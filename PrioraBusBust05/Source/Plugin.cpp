#include <JuceHeader.h>
#include "BinaryData.h"
#include <array>
#include <cmath>
#include <memory>

using APVTS = juce::AudioProcessorValueTreeState;

class PrioraBusProcessor final : public juce::AudioProcessor
{
public:
    PrioraBusProcessor()
      : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "PRIORA", makeLayout()) {}

    APVTS state;
    static APVTS::ParameterLayout makeLayout()
    {
        APVTS::ParameterLayout p;
        auto add=[&p](const char* id, const char* label,float minimum,float maximum,float step,float def)
        {p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},label,
                                juce::NormalisableRange<float>{minimum,maximum,step},def));};
        add("boost","Bass Boost (dB)",0,24,0.1f,12);
        add("frequency","Low End (Hz)",30,200,1,65);
        add("bust","Bust / Distortion",0,100,1,55);
        add("mix","Dry Wet (%)",0,100,1,100);
        p.add(std::make_unique<juce::AudioParameterChoice>(
              juce::ParameterID{"mode",1},"Bass Mode",
              juce::StringArray{"Stock","Priora","Street","Max"},1));
        return p;
    }

    float value(const char* id) const
    {if(auto* p=state.getRawParameterValue(id))return p->load();return 0.f;}

    void change(const char* id,float value)
    {if(auto* p=state.getParameter(id))p->setValueNotifyingHost(p->convertTo0to1(value));}

    void selectMode(int index)
    {
        change("mode",(float)index);
        switch(index)
        {
        case 0: change("boost",0);change("frequency",65);change("bust",0);break;
        case 1: change("boost",12);change("frequency",65);change("bust",55);break;
        case 2: change("boost",16);change("frequency",75);change("bust",72);break;
        default:change("boost",22);change("frequency",88);change("bust",92);break;
        }
    }

    void prepareToPlay(double sr,int blockSize) override
    {
        sampleRate=juce::jmax(1.0,sr);
        juce::dsp::ProcessSpec spec{sampleRate,(juce::uint32)juce::jmax(1,blockSize),1};
        for(auto& f:shelf){f.prepare(spec);f.reset();}
        for(auto& f:low){f.prepare(spec);f.reset();}
        juce::ignoreUnused(blockSize);
    }
    void releaseResources() override
    {for(auto& f:shelf)f.reset(); for(auto& f:low)f.reset();}

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        const auto a=l.getMainOutputChannelSet();
        return (a==juce::AudioChannelSet::mono()||a==juce::AudioChannelSet::stereo())
              && l.getMainInputChannelSet()==a;
    }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const float boost=value("boost"),freq=value("frequency"),bust=value("bust")/100.f,
                    wetMix=value("mix")/100.f;
        const int mode=(int)std::round(value("mode"));
        const float effectiveBoost=(mode==0?0.f:boost);
        const float distAmount=(mode==0?0.f:bust);
        const float gain=std::pow(10.f,effectiveBoost/20.f);
        const float safeGain=std::pow(10.f,-effectiveBoost*.26f/20.f);
        const float drive=1.f+distAmount*18.f;
        const float blend=(0.14f+0.72f*distAmount);
        const float corner=juce::jlimit(30.f,200.f,freq);
        auto ls=juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate,corner,.72f,gain);
        auto lp=juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate,juce::jmin(260.f,corner*1.75f),.707f);
        for(int ch=0;ch<juce::jmin(2,buffer.getNumChannels());++ch)
        {
            *shelf[(size_t)ch].coefficients=*ls;
            *low[(size_t)ch].coefficients=*lp;
            float* samples=buffer.getWritePointer(ch);
            for(int i=0;i<buffer.getNumSamples();++i)
            {
                float dry=samples[i];
                float boosted=shelf[(size_t)ch].processSample(dry);
                float bass=low[(size_t)ch].processSample(boosted);
                float dirty=std::tanh(bass*drive)/(1.f+.12f*drive);
                float x=boosted + (dirty-bass)*blend;
                x *= safeGain;
                // Peak protection with a continuously differentiable tanh soft clip.
                x = std::tanh(x*1.35f)/1.35f;
                samples[i]=juce::jlimit(-1.f,1.f,dry*(1.f-wetMix)+x*wetMix);
            }
        }
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
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
    void getStateInformation(juce::MemoryBlock& dst) override
    {if(auto xml=state.copyState().createXml())copyXmlToBinary(*xml,dst);}
    void setStateInformation(const void* data,int bytes) override
    {if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));}
private:
    double sampleRate=44100;
    std::array<juce::dsp::IIR::Filter<float>,2> shelf,low;
};

class PrioraBusEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrioraBusEditor(PrioraBusProcessor& p):AudioProcessorEditor(&p),processor(p)
    {
        art=juce::ImageCache::getFromMemory(BinaryData::background_jpg,BinaryData::background_jpgSize);
        setSize(960,640);
        startTimerHz(25);
    }
    void resized() override {}
    void paint(juce::Graphics& g) override
    {
        if(art.isValid())g.drawImage(art,getLocalBounds().toFloat());
        else g.fillAll(juce::Colour(0xff09090b));
        // Real-time overlay keeps controls responsive while retaining the approved image.
        const float scaleX=getWidth()/1536.f,scaleY=getHeight()/1024.f;
        g.saveState();
        g.addTransform(juce::AffineTransform::scale(scaleX,scaleY));
        auto overwrite=[&](juce::Rectangle<float> rect,const juce::String& text,int fontSize, juce::Justification justification)
        {
            g.setColour(juce::Colour(0xff121216));
            g.fillRoundedRectangle(rect,6.f);
            g.setColour(juce::Colour(0xfff4f5f7));
            g.setFont(juce::Font(juce::FontOptions((float)fontSize,juce::Font::bold)));
            g.drawText(text,rect,justification,true);
        };
        overwrite({109.f,624.f,330.f,69.f},"+"+juce::String(processor.value("boost"),1)+" dB",48,juce::Justification::centred);
        overwrite({1059.f,624.f,285.f,69.f},juce::String((int)processor.value("frequency"))+" Hz",47,juce::Justification::centred);
        overwrite({700.f,784.f,141.f,35.f},juce::String((int)processor.value("bust"))+"%",28,juce::Justification::centred);
        overwrite({1389.f,900.f,95.f,52.f},juce::String((int)processor.value("mix"))+"%",35,juce::Justification::centred);
        const float boost=processor.value("boost")/24.f;
        const float freq=(processor.value("frequency")-30.f)/170.f;
        const float mix=processor.value("mix")/100.f;
        auto bar=[&](float x,float y,float len,float progress)
        {
            g.setColour(juce::Colour(0xff111114));g.fillRoundedRectangle(x,y,len,16.f,8);
            g.setColour(juce::Colour(0xffff1626));g.fillRoundedRectangle(x,y,juce::jmax(4.f,len*progress),16.f,8);
            g.setColour(juce::Colours::white.withAlpha(.85f));
            g.fillEllipse(juce::jlimit(x,x+len,x+len*progress)-8.f,y-6.f,28.f,28.f);
        };
        bar(108.f,720.f,405.f,boost);
        bar(1043.f,720.f,396.f,freq);
        bar(1037.f,916.f,360.f,mix);
        const int mode=(int)processor.value("mode");
        const float x[]={179.f,345.f,516.f,685.f};
        g.setColour(juce::Colour(0xffff2335));
        g.drawRoundedRectangle(x[juce::jlimit(0,3,mode)],899.f,151.f,48.f,7.f,3);
        g.restoreState();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p=toArt(e.position); dragStart=p; initialBust=processor.value("bust");
        active=hit(p);
        if(active==4)
        {
            int mode=juce::jlimit(0,3,(int)((p.x-179)/168.f));
            if(p.x>=179&&p.x<=836)processor.selectMode(mode);
            active=0;
        }
        else if(active>=1&&active<=3)apply(p);
        else if(active==5)apply(p);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(active==0)return;
        apply(toArt(e.position));
    }
    void mouseUp(const juce::MouseEvent&) override{active=0;}
private:
    juce::Point<float> toArt(juce::Point<float> p) const
    {return {p.x*1536.f/getWidth(),p.y*1024.f/getHeight()};}
    int hit(juce::Point<float> p) const
    {
        if(p.y>860&&p.y<980&&p.x>140&&p.x<845)return 4;
        if(p.x>1000&&p.x<1500&&p.y>850&&p.y<985)return 5;
        if(p.x>970&&p.x<1510&&p.y>550&&p.y<830)return 2;
        if(p.x>55&&p.x<565&&p.y>550&&p.y<830)return 1;
        if(p.x>550&&p.x<980&&p.y>475&&p.y<870)return 3;
        return 0;
    }
    void apply(juce::Point<float> p)
    {
        switch(active)
        {
            case 1:processor.change("boost",juce::jlimit(0.f,24.f,(p.x-108.f)/405.f*24.f));break;
            case 2:processor.change("frequency",juce::jlimit(30.f,200.f,30.f+(p.x-1043.f)/396.f*170.f));break;
            case 3:processor.change("bust",juce::jlimit(0.f,100.f,initialBust+(dragStart.y-p.y)*.55f));break;
            case 5:processor.change("mix",juce::jlimit(0.f,100.f,(p.x-1037.f)/360.f*100.f));break;
            default:break;
        }
        repaint();
    }
    void timerCallback() override{repaint();}
    PrioraBusProcessor& processor;
    juce::Image art;
    int active=0;
    float initialBust=55.f;
    juce::Point<float> dragStart;
};

juce::AudioProcessorEditor* PrioraBusProcessor::createEditor(){return new PrioraBusEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new PrioraBusProcessor();}
