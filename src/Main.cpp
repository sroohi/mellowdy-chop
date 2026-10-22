#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/MainComponent.h"

namespace samplify {
class SamplifyApplication final : public juce::JUCEApplication, private juce::MenuBarModel,
                                 private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Samplify"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& arguments) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        window = std::make_unique<MainWindow>();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(this);
       #else
        window->setMenuBar(this);
       #endif
        if (arguments.startsWith("--smoke-test ")) {
            smokeDirectory = juce::File(arguments.fromFirstOccurrenceOf(" ", false, false).trim().unquoted());
            startTimer(100);
        }
    }
    void shutdown() override {
        stopTimer();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
       #endif
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {
        if (window) window->toFront(true);
    }
    juce::StringArray getMenuBarNames() override { return { "File", "Audio", "Help" }; }
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
        juce::PopupMenu menu;
        if (index == 0) {
            menu.addItem(1, "Import Audio...");
            menu.addItem(2, "Open Session...");
            menu.addItem(3, "Save Session...");
            menu.addSeparator();
            menu.addItem(4, "Export Selected Pad...");
            menu.addItem(5, "Export All Pads...");
        } else if (index == 1) {
            menu.addItem(6, "Audio & MIDI Settings...");
            menu.addItem(7, "Stop All Voices");
            menu.addItem(8, "Load Demo");
        } else {
            menu.addItem(9, "About Samplify");
        }
        return menu;
    }
    void menuItemSelected(int id, int) override {
        if (!window) return;
        auto& content = window->content();
        switch (id) {
            case 1: content.importAudio(); break;
            case 2: content.openSession(); break;
            case 3: content.saveSession(); break;
            case 4: content.exportSelected(); break;
            case 5: content.exportAll(); break;
            case 6: content.showAudioSettings(); break;
            case 7: content.stopPlayback(); break;
            case 8: content.loadDemo(); break;
            case 9: juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Samplify by MP Audio", "Standalone sampler / version 0.1.0\n\n"
                "Import audio, find chops, play pads and export WAV files.\n"
                "Stem controls use imported, aligned stem files. Neural separation is not included.\n\n"
                "Licensed under GPL-3.0. Built with JUCE 8."); break;
            default: break;
        }
    }

private:
    void timerCallback() override {
        if (!window || ++smokeAttempts > 600) {
            juce::Logger::writeToLog("Samplify UI smoke test timed out waiting for analysed audio and rendered pads");
            setApplicationReturnValue(1); stopTimer(); quit(); return;
        }
        auto& main = window->content();
        if (!main.isReadyForSmokeTest()) return;
        stopTimer();
        bool success = smokeDirectory.createDirectory().wasOk();
        auto image = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
        auto output = smokeDirectory.getChildFile("Samplify.png").createOutputStream();
        juce::PNGImageFormat png;
        if (output) { output->setPosition(0); output->truncate(); }
        success = output && png.writeImageToStream(image, *output) && success;
        output.reset();
        const auto normalWidth = main.getWidth(), normalHeight = main.getHeight();
        main.setSize(1120, 756);
        auto compact = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
        auto compactOutput = smokeDirectory.getChildFile("Samplify-compact.png").createOutputStream();
        if (compactOutput) { compactOutput->setPosition(0); compactOutput->truncate(); }
        success = compactOutput && png.writeImageToStream(compact, *compactOutput) && success;
        compactOutput.reset();
        main.setSize(normalWidth, normalHeight);
        auto* report = new juce::DynamicObject();
        report->setProperty("demoAnalysedAndPadsRendered", main.isReadyForSmokeTest());
        report->setProperty("width", main.getWidth()); report->setProperty("height", main.getHeight());
       #if JUCE_MAC
        report->setProperty("macOSRun", true);
       #else
        report->setProperty("macOSRun", false);
       #endif
        report->setProperty("screenshotSaved", success);
        success = smokeDirectory.getChildFile("smoke.json").replaceWithText(juce::JSON::toString(juce::var(report))) && success;
        juce::Logger::writeToLog(success ? "Samplify UI smoke test passed" : "Samplify UI smoke test failed writing evidence");
        setApplicationReturnValue(success ? 0 : 1); quit();
    }
    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow() : DocumentWindow("Samplify - MP Audio", juce::Colour(0xff171a1c), allButtons) {
            setUsingNativeTitleBar(true);
            auto* main = new MainComponent();
            setContentOwned(main, true);
            setResizable(true, false);
            setResizeLimits(1120, 780, 2560, 1800);
            centreWithSize(1360, 900);
            setVisible(true);
        }
        MainComponent& content() { return *static_cast<MainComponent*>(getContentComponent()); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<MainWindow> window;
    juce::File smokeDirectory;
    int smokeAttempts = 0;
};
}

START_JUCE_APPLICATION(samplify::SamplifyApplication)
