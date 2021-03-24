/**
 * @file
 */

#ifndef NETWORKLAYER_ERRMACRO_HH
#define NETWORKLAYER_ERRMACRO_HH

#include <rdma/fi_errno.h>
#include <kvcg_log2.hh>
#include <unistd.h>

#define MAJOR_VERSION_USED 1
#define MINOR_VERSION_USED 9

#define ERRCHK(x) error_check((x), __FILENAME__, __LINE__);

inline void error_check(int err, std::string file, int line) {
    if (err) {
        DO_LOG(ERROR) << "errno (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        _exit(1);
    }
}

#define SAFE_CALL(ans) err_negative_check((ans), __FILE__, __LINE__)

inline int err_negative_check(int err, const char *file, int line, bool abort = true) {
    if (err < 0) {
        DO_LOG(ERROR) << "errno (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        _exit(1);
    }
    return err;
}

#define ERRREPORT(x) error_report((x), __FILE__, __LINE__);

inline bool error_report(int err, std::string file, int line) {
    if (err) {
        DO_LOG(TRACE) << "errno (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        return false;
    }
    return true;
}


#endif //NETWORKLAYER_ERRMACRO_HH
