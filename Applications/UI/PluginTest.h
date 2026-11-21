#pragma once

// TEMPORARY test code, behind the "Test" button in the transport bar. Remove it with the button.

#include "Session.h"

namespace PluginTest
{
    /** Bank 2, slot 1 ("Metallica"): prints the plugin's preset name and gain to std::cout, halves
        the gain and saves the preset as User/Metallica Copy. Message thread.
    */
    void run (Session& session);
}
