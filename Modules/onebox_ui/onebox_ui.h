/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer and juce_add_module() to configure it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 onebox_ui
  vendor:             onebox
  version:            0.1.0
  name:               OneBox UI
  description:        Reusable dark "pro audio" UI components: theme, windows, panels, labels, buttons, knobs and faders.
  dependencies:       juce_gui_basics
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

/*  OneBox UI
    =========

    A vector-only theme (no image assets, sharp at any scale) and the basic components built on it.
    Everything lives in namespace onebox::ui.

    Quick start:

        // Every component inside a ui::Window picks up the theme automatically.
        auto window = std::make_unique<onebox::ui::Window> ("OneBoxBand");
        window->setContent (std::make_unique<MyMainComponent>(), 900, 600);

        // Inside MyMainComponent:
        onebox::ui::Panel    mixer { "Mixer" };
        onebox::ui::Label    title { "Song 1", onebox::ui::Label::Style::Title };
        onebox::ui::Button   play  { "Play", onebox::ui::Button::Variant::Accent };
        onebox::ui::Knob     depth { "Depth" };
        onebox::ui::ChannelFader trackVolume;

    Outside a ui::Window, call setLookAndFeel (&onebox::ui::LookAndFeel::getShared()) on your top-level
    component (and setLookAndFeel (nullptr) in its destructor).
*/

#pragma once
#define ONEBOX_UI_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>

#include "components/onebox_ChannelFader.h"
#include "theme/onebox_LookAndFeel.h"
#include "components/onebox_Window.h"
#include "components/onebox_Panel.h"
#include "components/onebox_Label.h"
#include "components/onebox_Button.h"
#include "components/onebox_Knob.h"
#include "components/onebox_Fader.h"
#include "components/onebox_SegmentedControl.h"
#include "components/onebox_IconButton.h"
