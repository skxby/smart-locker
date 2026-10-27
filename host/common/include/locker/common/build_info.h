#ifndef LOCKER_COMMON_BUILD_INFO_H
#define LOCKER_COMMON_BUILD_INFO_H

/*
 * S1-01 host infrastructure: link-time contract identity.
 * Lets the service print and tests assert which contract baseline the binary was
 * compiled against; the value is the generated macro, not a second copy.
 */

#ifdef __cplusplus
extern "C" {
#endif

const char *locker_common_contract_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_BUILD_INFO_H */
