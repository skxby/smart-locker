#include "test_support.h"

#include "locker/common/build_info.h"

/* The linked binary must report exactly the generated contract baseline. */
static void test_contract_version_matches_header(void) {
    const char *version = locker_common_contract_version();
    TEST_CHECK(version != NULL);
    TEST_CHECK_STRING(LOCKER_CONTRACT_VERSION, version);
    TEST_CHECK(strlen(version) == 5u);
    TEST_CHECK(version[0] >= '0' && version[0] <= '9');
    TEST_CHECK(version[1] == '.');
}

int main(void) {
    test_contract_version_matches_header();
    TEST_REPORT("common.build_info");
}
