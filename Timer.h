// Copyright 2011-2015 by SAP SE
//
// Authors:
//     Marcus Paradies <m.paradies@sap.com>

#ifndef SRC_UTILS_TIMER_H_
#define SRC_UTILS_TIMER_H_

#include <chrono>

using std::chrono::system_clock;
using std::chrono::time_point;

namespace utils {

    typedef std::chrono::duration<int64_t,std::micro> microseconds_type;

    /**
    * @brief
    * @author Marcus Paradies
    */
    class Timer{
    public:
        Timer(){}
        ~Timer(){}

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline void start(){ m_start = std::chrono::high_resolution_clock::now(); }

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline void stop(){ m_end = std::chrono::high_resolution_clock::now(); }

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline int getMicroSeconds() {
            m_duration = std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start);
            return m_duration.count();
        }

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline double getMilliSeconds() {
            m_duration = std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start);
            return static_cast<double>(m_duration.count())/1000; }

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline bool isMicroSeconds(){
            if (m_duration.count() > 1000){
                return true;
            }
            return false;
        }

        /**
         * @brief
         * @author Marcus Paradies
         */
        inline void reset(){ }
    private:
        time_point<std::chrono::high_resolution_clock> m_start;
        time_point<std::chrono::high_resolution_clock> m_end;
        microseconds_type m_duration;
    };
}

#endif  // SRC_UTILS_TIMER_H_
