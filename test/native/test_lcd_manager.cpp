// Native tests for LCDManager message merge behavior (using FakeLCDManager)

#include "FakeLCDManager.h"
#include "test_helpers.h"

int test_recent_two_line_message_combines_to_four_lines() {
    LCDManager lcd;
    lcd.updateScreen("Old top", "Old bottom");
    lcd.advanceTimeMs(1000);

    lcd.updateScreen("New top", "New bottom");

    TEST_ASSERT_EQ(lcd.updateCount, 2);
    TEST_ASSERT_EQ(lcd.lastLineCount, 4);
    TEST_ASSERT_EQ(lcd.lastLine1, std::string("Old top"));
    TEST_ASSERT_EQ(lcd.lastLine2, std::string("Old bottom"));
    TEST_ASSERT_EQ(lcd.lastLine3, std::string("New top"));
    TEST_ASSERT_EQ(lcd.lastLine4, std::string("New bottom"));
    return 0;
}

int test_existing_four_line_message_is_not_impacted() {
    LCDManager lcd;
    lcd.updateScreen("A1", "A2", "A3", "A4");
    lcd.advanceTimeMs(1000);

    lcd.updateScreen("B1", "B2");

    TEST_ASSERT_EQ(lcd.updateCount, 2);
    TEST_ASSERT_EQ(lcd.lastLineCount, 2);
    TEST_ASSERT_EQ(lcd.lastLine1, std::string("B1"));
    TEST_ASSERT_EQ(lcd.lastLine2, std::string("B2"));
    TEST_ASSERT_EQ(lcd.lastLine3, std::string(""));
    TEST_ASSERT_EQ(lcd.lastLine4, std::string(""));
    return 0;
}

int test_old_two_line_message_does_not_combine_after_six_seconds() {
    LCDManager lcd;
    lcd.updateScreen("Earlier top", "Earlier bottom");
    lcd.advanceTimeMs(6001);

    lcd.updateScreen("Later top", "Later bottom");

    TEST_ASSERT_EQ(lcd.updateCount, 2);
    TEST_ASSERT_EQ(lcd.lastLineCount, 2);
    TEST_ASSERT_EQ(lcd.lastLine1, std::string("Later top"));
    TEST_ASSERT_EQ(lcd.lastLine2, std::string("Later bottom"));
    TEST_ASSERT_EQ(lcd.lastLine3, std::string(""));
    TEST_ASSERT_EQ(lcd.lastLine4, std::string(""));
    return 0;
}

int main() {
    int passed = 0, failed = 0, total = 0;

    printf("\n=== LCDManager Behavior Tests ===\n\n");
    RUN_TEST(test_recent_two_line_message_combines_to_four_lines);
    RUN_TEST(test_existing_four_line_message_is_not_impacted);
    RUN_TEST(test_old_two_line_message_does_not_combine_after_six_seconds);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);
    return failed > 0 ? 1 : 0;
}
