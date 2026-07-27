include(KRR_AddTest)

krr_add_test(
    flux Scene ContextTests
    SOURCES ContextTests.cpp
    HARD_DEPENDENCIES flux)
