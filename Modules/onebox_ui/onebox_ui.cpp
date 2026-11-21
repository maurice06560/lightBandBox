#ifdef ONEBOX_UI_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of OneBox UI cpp file"
#endif

#include "onebox_ui.h"

#include "theme/onebox_LookAndFeel.cpp"
#include "components/onebox_ChannelFader.cpp"
#include "components/onebox_Window.cpp"
#include "components/onebox_Panel.cpp"
#include "components/onebox_Label.cpp"
#include "components/onebox_Button.cpp"
#include "components/onebox_Knob.cpp"
#include "components/onebox_Fader.cpp"
#include "components/onebox_SegmentedControl.cpp"
#include "components/onebox_IconButton.cpp"
