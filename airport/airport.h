#ifndef INC_airport_h
#define INC_airport_h

#include <ross.h>
#include <memory>

#define MEAN_DEPARTURE 30.0
#define MEAN_LAND 10.0

//typedef enum airport_event_t airport_event_t;
//typedef class airport_state airport_state;
typedef struct airport_message airport_message;

enum airport_event_t
{
	ARRIVAL = 1, 
	DEPARTURE,
	LAND
};

class airport_state : public LP_State
{
    std::shared_ptr<int>        landings;
    std::shared_ptr<int>		planes_in_the_sky;
    std::shared_ptr<int>		planes_on_the_ground;

    std::shared_ptr<tw_stime>	waiting_time;
    std::shared_ptr<tw_stime>	furthest_flight_landing;

public:
    airport_state() = default;
    airport_state(const airport_state &a) = default;

    airport_state(airport_state&& rhs) = default;              // move constructor
    airport_state& operator=(airport_state&& rhs) = default;   // move assignment operator

    int get_landings() const { return *landings.get(); }
    void set_landings(int l) { landings = std::make_shared<int>(l); }

    int get_planes_in_the_sky() const { return *planes_in_the_sky.get(); }
    void set_planes_in_the_sky(int p) { planes_in_the_sky = std::make_shared<int>(p); }

    int get_planes_on_the_ground() const { return *planes_on_the_ground.get(); }
    void set_planes_on_the_ground(int p) { planes_on_the_ground = std::make_shared<int>(p); }

    tw_stime get_waiting_time() const { return *waiting_time.get(); }
    void set_waiting_time(tw_stime w) { waiting_time = std::make_shared<tw_stime>(w); }

    tw_stime get_furthest_flight_landing() const { return *furthest_flight_landing.get(); }
    void set_furthest_flight_landing(tw_stime f) { furthest_flight_landing = std::make_shared<tw_stime>(f); }

    airport_state * clone() const override
    {
        return new airport_state(*this);
    }
};

struct airport_message
{
	airport_event_t	 type;

	tw_stime	 waiting_time;
	tw_stime	 saved_furthest_flight_landing;
};

static tw_stime lookahead = 0.00000001;
static tw_lpid	 nlp_per_pe = 1024;
static tw_stime	 mean_flight_time = 1;
static int	 opt_mem = 1000;
static int	 planes_per_airport = 1;

static tw_stime	 wait_time_avg = 0.0;

#endif
