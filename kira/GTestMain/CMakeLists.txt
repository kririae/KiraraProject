include(KRR_AddModule)

krr_add_module(
    kira GTestMain
    SOURCES GTestMain.cpp
    HARD_DEPENDENCIES gtest)
