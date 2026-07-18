#ifndef __ADAM_UTILS_PROF_HPP__
#define __ADAM_UTILS_PROF_HPP__


#include <string>
#include <gperftools/profiler.h>


namespace adam::utils {


class Prof {
public:
    explicit
    Prof(const std::string& fname) noexcept {
        if (fname.length() > 0) {
            ::ProfilerStart(fname.c_str());
        }
    }


    ~Prof() noexcept {
        ::ProfilerStop();
    }

}; // class Prof;

    
} // namespace adam::utils



#endif // __ADAM_UTILS_PROF_HPP__