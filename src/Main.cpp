#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/MainComponent.h"

namespace samplify {
class SamplifyApplication final : public juce::JUCEApplication, private juce::MenuBarModel,
                                 private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Samplify"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
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
                "Samplify by MP Audio", "Standalone sampler / version 0.2.0\n\n"
                "Cue performance and editing based on the documented Serato Sample 2.x workflow.\n"
                "Independent Rubber Band pitch/time and native HTDemucs four-stem processing.\n"
                "This app does not contain Serato's proprietary engines.\n\n"
                "GPL-3.0. Built with JUCE 8; Rubber Band and Demucs notices are bundled."); break;
            default: break;
        }
    }

private:
    bool saveSmokeImage(const juce::String& name) {
        auto& main = window->content();
        auto image = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
        auto output = smokeDirectory.getChildFile(name).createOutputStream();
        if (!output) return false;
        output->setPosition(0); output->truncate();
        return juce::PNGImageFormat().writeImageToStream(image, *output);
    }
    void timerCallback() override {
        if (!window) return;
        auto& main = window->content();
        if (++smokeAttempts > 2400) {
            finishSmoke("Timed out waiting for the UI workflow and native stem separation"); return;
        }
        try {
            if (smokeStage == 0) {
                if (smokeDirectory.createDirectory().failed()) throw std::runtime_error("Cannot create smoke evidence directory");
                blankScreenshotSaved = saveSmokeImage("Samplify-empty.png");
                main.loadDemo(); smokeStage = 1;
            } else if (smokeStage == 1 && main.isReadyForSmokeTest()) {
                workflowChecks = main.runWorkflowSmoke();
                main.beginStemSmoke(); smokeStage = 2;
            } else if (smokeStage == 2 && main.isStemSmokeReady()) {
                stemWorkflowChecks = main.stemSmokeResult();
                if (auto* checks = stemWorkflowChecks.getDynamicObject()) {
                    for (const auto& item : checks->getProperties())
                        if (!item.value.isBool() || !static_cast<bool>(item.value))
                            throw std::runtime_error("Native stem UI workflow failed: " + item.name.toString().toStdString());
                } else throw std::runtime_error("Native stem UI workflow returned no checks");
                stemSeparationCompleted = true; finishSmoke({});
            }
        } catch (const std::exception& e) { finishSmoke(e.what()); }
    }
    void finishSmoke(const juce::String& error) {
        stopTimer();
        auto& main = window->content();
        bool success = error.isEmpty() && blankScreenshotSaved && stemSeparationCompleted;
        const bool normalSaved = saveSmokeImage("Samplify.png");
        const auto normalWidth = main.getWidth(), normalHeight = main.getHeight();
        main.setSize(960, 720);
        const bool compactSaved = saveSmokeImage("Samplify-compact.png");
        main.setSize(normalWidth, normalHeight);
        auto* report = new juce::DynamicObject();
        report->setProperty("demoAnalysedAndPadsRendered", main.isReadyForSmokeTest());
        report->setProperty("workflowChecks", workflowChecks);
        report->setProperty("stemWorkflowChecks", stemWorkflowChecks);
        report->setProperty("stemSeparationCompleted", stemSeparationCompleted);
        report->setProperty("blankScreenshotSaved", blankScreenshotSaved);
        report->setProperty("error", error);
        report->setProperty("width", main.getWidth()); report->setProperty("height", main.getHeight());
       #if JUCE_MAC
        report->setProperty("macOSRun", true);
       #else
        report->setProperty("macOSRun", false);
       #endif
        report->setProperty("screenshotSaved", normalSaved && compactSaved);
        success = success && normalSaved && compactSaved;
        success = smokeDirectory.getChildFile("smoke.json").replaceWithText(juce::JSON::toString(juce::var(report))) && success;
        juce::Logger::writeToLog(success ? "Samplify UI smoke test passed" : "Samplify UI smoke test failed: " + error);
        setApplicationReturnValue(success ? 0 : 1); quit();
    }
    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow() : DocumentWindow("Samplify - MP Audio", juce::Colour(0xff171a1c), allButtons) {
            setUsingNativeTitleBar(true);
            auto* main = new MainComponent();
            setContentOwned(main, true);
            setResizable(true, false);
            setResizeLimits(960, 744, 2560, 1800);
            centreWithSize(1280, 984);
            setVisible(true);
        }
        MainComponent& content() { return *static_cast<MainComponent*>(getContentComponent()); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<MainWindow> window;
    juce::File smokeDirectory;
    juce::var workflowChecks, stemWorkflowChecks;
    int smokeAttempts = 0, smokeStage = 0;
    bool blankScreenshotSaved = false, stemSeparationCompleted = false;
};
}

START_JUCE_APPLICATION(samplify::SamplifyApplication)
