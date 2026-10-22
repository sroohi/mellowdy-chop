#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/StudioComponent.h"

namespace samplify {
class SamplifyApplication final : public juce::JUCEApplication, private juce::MenuBarModel,
                                 private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Samplify"; }
    const juce::String getApplicationVersion() override { return "0.3.0"; }
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
            menu.addSeparator();
            menu.addItem(10, "Import Audio Loop...");
            menu.addItem(11, "Bounce Selected Pattern...");
        } else if (index == 1) {
            menu.addItem(6, "Audio & MIDI Settings...");
            menu.addItem(7, "Stop All Voices");
            menu.addItem(8, "Load Demo");
            menu.addItem(12, "Start / Stop Recording Master Mix...");
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
            case 10: window->studio().workstation().importAudioClip(); break;
            case 11: window->studio().workstation().bounceSelectedPattern(); break;
            case 12: window->studio().workstation().toggleMasterRecording(); break;
            case 9: juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Samplify by MP Audio", "Native sampling workstation / version 0.3.0\n\n"
                "32-cue sampler, pattern recording, arrangement, FX and live performance.\n"
                "Native HTDemucs Stems, Rubber Band pitch/time, built-in effects and AU/VST3 hosting.\n"
                "This app does not contain Serato's proprietary engines.\n\n"
                "GPL-3.0. Built with JUCE 8; Rubber Band and Demucs notices are bundled."); break;
            default: break;
        }
    }

private:
    bool saveSmokeImage(const juce::String& name) {
        auto& main = window->studio();
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
                stemSeparationCompleted = true;
                workstationChecks = window->studio().workstation().runWorkflowSmoke();
                if (auto* checks = workstationChecks.getDynamicObject()) {
                    if (checks->getProperties().size() < 10) throw std::runtime_error("Workstation smoke returned too few checks");
                    for (const auto& item : checks->getProperties())
                        if (!item.value.isBool() || !static_cast<bool>(item.value))
                            throw std::runtime_error("Workstation UI workflow failed: " + item.name.toString().toStdString());
                } else throw std::runtime_error("Workstation UI workflow returned no checks");
                finishSmoke({});
            }
        } catch (const std::exception& e) { finishSmoke(e.what()); }
    }
    void finishSmoke(const juce::String& error) {
        stopTimer();
        auto& main = window->content();
        auto& shell = window->studio();
        shell.showPage(0);
        bool success = error.isEmpty() && blankScreenshotSaved && stemSeparationCompleted;
        const bool normalSaved = saveSmokeImage("Samplify.png");
        const auto normalWidth = shell.getWidth(), normalHeight = shell.getHeight();
        shell.setSize(1040, 830);
        const bool compactSaved = saveSmokeImage("Samplify-compact.png");
        shell.setSize(normalWidth, normalHeight);
        bool pagesSaved = true;
        constexpr const char* names[] { "Samplify-arrange.png", "Samplify-patterns.png", "Samplify-fx.png", "Samplify-perform.png" };
        for (int i = 0; i < 4; ++i) { shell.showPage(i + 1); pagesSaved = saveSmokeImage(names[i]) && pagesSaved; }
        shell.showPage(0);
        auto* report = new juce::DynamicObject();
        report->setProperty("demoAnalysedAndPadsRendered", main.isReadyForSmokeTest());
        report->setProperty("workflowChecks", workflowChecks);
        report->setProperty("stemWorkflowChecks", stemWorkflowChecks);
        report->setProperty("workstationChecks", workstationChecks);
        report->setProperty("workstationScreenshotsSaved", pagesSaved);
        report->setProperty("stemSeparationCompleted", stemSeparationCompleted);
        report->setProperty("blankScreenshotSaved", blankScreenshotSaved);
        report->setProperty("error", error);
        report->setProperty("width", shell.getWidth()); report->setProperty("height", shell.getHeight());
       #if JUCE_MAC
        report->setProperty("macOSRun", true);
       #else
        report->setProperty("macOSRun", false);
       #endif
        report->setProperty("screenshotSaved", normalSaved && compactSaved && pagesSaved);
        success = success && normalSaved && compactSaved && pagesSaved;
        success = smokeDirectory.getChildFile("smoke.json").replaceWithText(juce::JSON::toString(juce::var(report))) && success;
        juce::Logger::writeToLog(success ? "Samplify UI smoke test passed" : "Samplify UI smoke test failed: " + error);
        setApplicationReturnValue(success ? 0 : 1); quit();
    }
    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow() : DocumentWindow("Samplify - MP Audio", HardwareLookAndFeel::chassis(), allButtons) {
            setUsingNativeTitleBar(true);
            auto* main = new StudioComponent();
            setContentOwned(main, true);
            setResizable(true, false);
            setResizeLimits(1040, 830, 2560, 1800);
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = display ? display->userArea : juce::Rectangle<int>(1440, 1100);
            centreWithSize(juce::jlimit(1040, 1440, area.getWidth() - 60), juce::jlimit(830, 1060, area.getHeight() - 80));
            setVisible(true);
        }
        StudioComponent& studio() { return *static_cast<StudioComponent*>(getContentComponent()); }
        MainComponent& content() { return studio().sampler(); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    HardwareLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
    juce::File smokeDirectory;
    juce::var workflowChecks, stemWorkflowChecks, workstationChecks;
    int smokeAttempts = 0, smokeStage = 0;
    bool blankScreenshotSaved = false, stemSeparationCompleted = false;
};
}

START_JUCE_APPLICATION(samplify::SamplifyApplication)
