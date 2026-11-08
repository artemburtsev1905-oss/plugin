#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>

using APVTS = juce::AudioProcessorValueTreeState;

class VocalProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    VocalProcessor() : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                   .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
                   state(*this,nullptr,"VAA",parameterLayout()) {}
    APVTS state;

    static APVTS::ParameterLayout parameterLayout()
    {
        APVTS::ParameterLayout p;
        auto add = [&p](const char* id,const char* name,float lo,float hi,float step,float initial)
        { p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>{lo,hi,step},initial)); };
        add("threshold","Threshold",-48,-3,0.1f,-18);
        add("ratio","Ratio",1,10,0.05f,3);
        add("attack","Attack ms",1,100,0.5f,12);
        add("release","Release ms",20,800,1,110);
        add("makeup","Makeup dB",-12,12,0.1f,0);
        add("manualBpm","Manual BPM",40,240,1,120);
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"autoApply",1},"Auto Apply",true));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"enabled",1},"Compressor Enabled",true));
        return p;
    }
    float val(const char* id) const { if(auto* x=state.getRawParameterValue(id))return x->load();return 0; }
    void startAnalysis() { startFlag.store(true); }
    int status() const { return analysisStatus.load(); }
    int progress() const { return progressValue.load(); }
    bool fromHost() const { return validHostTempo.load(); }
    double tempo() const { return fromHost()?hostTempo.load():val("manualBpm"); }
    struct Proposal {float threshold=-18,ratio=3,attack=12,release=110,makeup=0;};
    Proposal proposal() const {return {suggestThreshold.load(),suggestRatio.load(),suggestAttack.load(),suggestRelease.load(),suggestMakeup.load()};}
    void applyProposal()
    {
        if(status()!=2) return;
        const auto p=proposal();
        const std::array<std::pair<const char*,float>,6> values {{{"threshold",p.threshold},{"ratio",p.ratio},{"attack",p.attack},{"release",p.release},{"makeup",p.makeup},{"enabled",1.f}}};
        for(const auto& v:values) if(auto* param=state.getParameter(v.first))
            param->setValueNotifyingHost(param->convertTo0to1(v.second));
    }
    void prepareToPlay(double sampleRate,int maxBlock) override
    {
        sampleHz=juce::jmax(1.0,sampleRate);
        windowSize=juce::jmax(1,(int)(sampleHz*.04));
        duration=(int64_t)(sampleHz*8);
        compressor.prepare({sampleHz,(juce::uint32)juce::jmax(1,maxBlock),(juce::uint32)juce::jmax(1,getTotalNumOutputChannels())});
        compressor.reset();
        analysisStatus.store(0); progressValue.store(0);
    }
    void releaseResources() override {compressor.reset();}
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        auto out=l.getMainOutputChannelSet();
        return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==l.getMainInputChannelSet();
    }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        bool tempoValid=false;
        if(auto* ph=getPlayHead()) if(auto position=ph->getPosition()) if(auto bpm=position->getBpm())
        {
            if(std::isfinite(*bpm)&&*bpm>10&&*bpm<400){ hostTempo.store(*bpm);tempoValid=true;}
        }
        validHostTempo.store(tempoValid);
        if(startFlag.exchange(false))
        {
            histogram.fill(0); active=0; winSamples=0; winEnergy=0; winPeak=0; peak=0; counted=0;
            analysisStatus.store(1);progressValue.store(0);
        }
        int n=buffer.getNumSamples();
        int ch=juce::jmin(buffer.getNumChannels(),getTotalNumInputChannels());
        if(status()==1 && ch>0)
        {
            for(int i=0;i<n;++i)
            {
                float x=0;for(int c=0;c<ch;++c)x+=buffer.getReadPointer(c)[i];
                x/=ch;winEnergy+=(double)x*x;winPeak=juce::jmax(winPeak,std::abs(x));
                if(++winSamples>=windowSize)finishWindow();
            }
            counted+=n;
            progressValue.store(juce::jlimit(0,100,(int)(100.0*counted/juce::jmax<int64_t>(1,duration))));
            if(counted>=duration)finalize();
        }
        if(val("enabled")>=.5f && ch>0)
        {
            compressor.setThreshold(val("threshold"));compressor.setRatio(val("ratio"));
            compressor.setAttack(val("attack"));compressor.setRelease(val("release"));
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            compressor.process(context);
            buffer.applyGain(juce::Decibels::decibelsToGain(val("makeup")));
        }
    }
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock& b) override
    {if(auto x=state.copyState().createXml())copyXmlToBinary(*x,b);}
    void setStateInformation(const void* data,int bytes) override
    {if(auto x=getXmlFromBinary(data,bytes))if(x->hasTagName(state.state.getType()))state.replaceState(juce::ValueTree::fromXml(*x));}

private:
    void handleAsyncUpdate() override {if(val("autoApply")>=.5f&&status()==2)applyProposal();}
    void finishWindow()
    {
        if(winSamples<1)return;
        float rmsDb=juce::Decibels::gainToDecibels((float)std::sqrt(winEnergy/winSamples),-100.f);
        if(rmsDb>-53.f)
        {
            int bin=juce::jlimit(0,72,(int)std::floor(rmsDb)+72);
            ++histogram[(size_t)bin];++active;peak=juce::jmax(peak,winPeak);
        }
        winSamples=0;winEnergy=0;winPeak=0;
    }
    float percentile(float f) const
    {
        int target=juce::jmax(1,(int)std::ceil(active*f)),sum=0;
        for(int i=0;i<73;++i){sum+=histogram[(size_t)i];if(sum>=target)return (float)i-72;}
        return -24;
    }
    void finalize()
    {
        if(winSamples>0)finishWindow();
        if(active<12){analysisStatus.store(3);return;}
        float p60=percentile(.60f),p80=percentile(.80f);
        float crest=juce::jlimit(0.f,30.f,juce::Decibels::gainToDecibels(peak,-100.f)-p80);
        float quarter=(float)(60000.0/juce::jmax(10.0,tempo()));
        float threshold=juce::jlimit(-38.f,-8.f,p60-5);
        float ratio=juce::jlimit(2.f,5.f,2.7f+.12f*(crest-10));
        float attack=juce::jlimit(5.f,30.f,12.f+1.1f*(crest-8));
        float release=juce::jlimit(55.f,240.f,quarter/4);
        float reduction=juce::jmax(0.f,p80-threshold)*(1-1/ratio);
        suggestThreshold.store(threshold); suggestRatio.store(ratio);suggestAttack.store(attack);
        suggestRelease.store(release);suggestMakeup.store(juce::jlimit(0.f,8.f,reduction*.6f));
        analysisStatus.store(2);progressValue.store(100);triggerAsyncUpdate();
    }
    juce::dsp::Compressor<float> compressor;
    std::atomic<bool> startFlag{false};
    std::atomic<int> analysisStatus{0},progressValue{0};
    std::atomic<double> hostTempo{120};
    std::atomic<bool> validHostTempo{false};
    std::atomic<float> suggestThreshold{-18},suggestRatio{3},suggestAttack{12},suggestRelease{110},suggestMakeup{0};
    double sampleHz=44100,winEnergy=0;
    int windowSize=1764,winSamples=0,active=0;
    int64_t counted=0,duration=352800;
    float winPeak=0,peak=0;
    std::array<int,73> histogram{};
};

class VocalEditor final : public juce::AudioProcessorEditor,private juce::Timer
{
public:
    explicit VocalEditor(VocalProcessor& p):juce::AudioProcessorEditor(&p),processor(p)
    {
        setSize(1000,745);
        auto setupButton=[this](juce::TextButton& b,juce::Colour color)
        {b.setColour(juce::TextButton::buttonColourId,color);b.setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(b);};
        setupButton(analyze,juce::Colour(0xff2385bd));setupButton(apply,juce::Colour(0xff29445e));
        analyze.onClick=[this]{processor.startAnalysis();};
        apply.onClick=[this]{processor.applyProposal();};
        for(auto* b:{&autoApply,&enabled})
        {b->setColour(juce::ToggleButton::textColourId,juce::Colour(0xffe4efff));b->setColour(juce::ToggleButton::tickColourId,juce::Colour(0xff53daf2));addAndMakeVisible(b);}
        autoAttach=std::make_unique<APVTS::ButtonAttachment>(p.state,"autoApply",autoApply);
        onAttach=std::make_unique<APVTS::ButtonAttachment>(p.state,"enabled",enabled);
        constexpr const char* ids[6]={"threshold","ratio","attack","release","makeup","manualBpm"};
        constexpr const char* names[6]={"THRESHOLD  /  dB","RATIO","ATTACK  /  ms","RELEASE  /  ms","MAKEUP  /  dB","MANUAL BPM"};
        for(int i=0;i<6;++i)
        {
            auto& s=sliders[(size_t)i];
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight,false,68,28);
            s.setColour(juce::Slider::trackColourId,juce::Colour(0xff48bde7));
            s.setColour(juce::Slider::thumbColourId,juce::Colour(0xffb2fbff));
            s.setColour(juce::Slider::textBoxTextColourId,juce::Colours::white);
            s.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff1a3146));
            s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff273d55));
            addAndMakeVisible(s);
            labels[(size_t)i].setText(names[i],juce::dontSendNotification);
            labels[(size_t)i].setColour(juce::Label::textColourId,juce::Colour(0xffafc3d9));
            labels[(size_t)i].setFont(juce::Font(juce::FontOptions(12.f,juce::Font::bold)));
            addAndMakeVisible(labels[(size_t)i]);
            attaches[(size_t)i]=std::make_unique<APVTS::SliderAttachment>(p.state,ids[i],s);
        }
        startTimerHz(12);
    }
    void resized() override
    {
        analyze.setBounds(40,181,190,44);apply.setBounds(242,181,175,44);
        autoApply.setBounds(442,185,235,36);
        enabled.setBounds(730,292,224,33);
        for(int i=0;i<5;++i){labels[(size_t)i].setBounds(42,336+i*55,184,28);sliders[(size_t)i].setBounds(229,333+i*55,716,32);}
        labels[5].setBounds(542,665,118,26);sliders[5].setBounds(654,660,282,35);
    }
    void paint(juce::Graphics& g) override
    {
        juce::Colour bg(0xff0a1220),surface(0xff142336),edge(0xff274058),accent(0xff52dded),muted(0xff9ab2c9);
        g.fillAll(bg);
        auto panel=[&](juce::Rectangle<float> r)
        {g.setColour(surface);g.fillRoundedRectangle(r,17);g.setColour(edge);g.drawRoundedRectangle(r,17,1.2f);};
        g.setColour(juce::Colour(0xffdceeff));g.setFont(juce::Font(juce::FontOptions(30.f,juce::Font::bold)));
        g.drawText("VOCAL  AUTO  ASSISTANT",40,24,920,47,juce::Justification::centredLeft);
        g.setFont(juce::Font(juce::FontOptions(13.f)));g.setColour(muted);
        g.drawText("VOCAL ENGINE   /   DYNAMICS ANALYZER   /   TEMPO-SYNCED FX TIMES",42,76,920,23,juce::Justification::centredLeft);
        g.setColour(accent);g.fillRoundedRectangle(40.f,111.f,920.f,3.f,2.f);
        panel({26,128,948,125});panel({26,266,948,355});panel({26,633,948,90});
        g.setColour(accent);g.setFont(juce::Font(juce::FontOptions(13.f,juce::Font::bold)));
        g.drawText("01   /   LIVE VOCAL ANALYSIS",41,142,400,26,juce::Justification::centredLeft);
        g.drawText("02   /   AUTO COMPRESSOR",41,283,440,30,juce::Justification::centredLeft);
        g.drawText("03   /   TEMPO ENGINE",41,645,380,27,juce::Justification::centredLeft);
        g.setColour(edge);g.drawLine(41,330,954,330,1);
        int st=processor.status();
        juce::String message=st==0?"READY  /  Play isolated vocal, then press ANALYZE 8s":
                        st==1?"LISTENING  /  "+juce::String(processor.progress())+"%":
                        st==2?"ANALYSIS COMPLETE  /  Compressor recommendations ready":
                              "NO SUFFICIENT VOCAL SIGNAL  /  Play louder audio and retry";
        g.setFont(juce::Font(juce::FontOptions(13.f,juce::Font::bold)));g.setColour(juce::Colour(0xffe5f0fc));
        g.drawText(message,42,229,900,22,juce::Justification::centredLeft);
        if(st==1){g.setColour(accent);g.fillRoundedRectangle(42.f,250.f,9.1f*processor.progress(),3.f,1.5f);}
        auto bpm=processor.tempo(),quarter=60000.0/juce::jmax(10.0,bpm);
        g.setColour(muted);g.setFont(juce::Font(juce::FontOptions(12.f)));
        g.drawText(juce::String::formatted("BPM %.0f  /  %s  |  DELAY 1/16 %.0f ms   1/8 %.0f ms   1/4 %.0f ms",bpm,
          processor.fromHost()?"FL STUDIO":"MANUAL",quarter/4,quarter/2,quarter),42,683,500,25,juce::Justification::centredLeft);
        g.drawText(juce::String::formatted("ROOM %.2fs    PLATE %.2fs    HALL %.2fs",quarter/1000,2*quarter/1000,4*quarter/1000),
           42,703,500,19,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff68829d));g.setFont(juce::Font(juce::FontOptions(11.f)));
        g.drawText("Reverb values are suggestions, not processed audio.",689,621,276,19,juce::Justification::centredRight);
    }
private:
    void timerCallback() override
    {
        apply.setEnabled(processor.status()==2);
        sliders[5].setEnabled(!processor.fromHost());
        repaint();
    }
    VocalProcessor& processor;
    juce::TextButton analyze{"ANALYZE 8s"},apply{"APPLY SETTINGS"};
    juce::ToggleButton autoApply{"AUTO APPLY"},enabled{"COMPRESSOR ON"};
    std::array<juce::Slider,6> sliders;
    std::array<juce::Label,6> labels;
    std::array<std::unique_ptr<APVTS::SliderAttachment>,6> attaches;
    std::unique_ptr<APVTS::ButtonAttachment> autoAttach,onAttach;
};

juce::AudioProcessorEditor* VocalProcessor::createEditor(){return new VocalEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new VocalProcessor();}
