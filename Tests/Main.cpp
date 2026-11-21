#include <juce_events/juce_events.h>

//==============================================================================
// Runs every JUCE UnitTest in the "MBox" category. Pass a test name to run only that test.
// Returns the number of failures, so CTest reports them.
//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
    {
        for (auto* test : juce::UnitTest::getTestsInCategory ("MBox"))
            if (test->getName() == argv[1])
                runner.runTests ({ test });
    }
    else
    {
        runner.runTestsInCategory ("MBox");
    }

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%d test group(s), %d failure(s)\n", runner.getNumResults(), failures);
    return failures;
}
