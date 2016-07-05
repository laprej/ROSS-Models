#include "airport.h"
#include <new>
/*
  airport.c
  Airport simulator
  20011003
  Justin M. LaPre

  2008/2/16
  Modified for ROSS 4.0
  David Bauer
*/

tw_peid
mapping(tw_lpid gid)
{
	return (tw_peid) gid / g_tw_nlp;
}

void
init(airport_state * s, tw_lp * lp)
{
  int i;
  tw_event *e;
  airport_message *m;

  new (s) airport_state;

  s->set_landings(0);
  s->set_planes_in_the_sky(0);
  s->set_planes_on_the_ground(planes_per_airport);
  s->set_waiting_time(0.0);
  s->set_furthest_flight_landing(0.0);

  for(i = 0; i < planes_per_airport; i++)
    {
      e = tw_event_new(lp->gid, tw_rand_exponential(&lp->cur_state->rng, MEAN_DEPARTURE), lp);
      m = (airport_message*)tw_event_data(e);
      m->type = DEPARTURE;
      tw_event_send(e);
    }
}

void
event_handler(airport_state * s, tw_bf * bf, airport_message * msg, tw_lp * lp)
{
  int rand_result;
  tw_lpid dst_lp;
  tw_stime ts;
  tw_event *e;
  airport_message *m;

  switch(msg->type)
    {

    case ARRIVAL:
      {
	// Schedule a landing in the future
	msg->saved_furthest_flight_landing = s->get_furthest_flight_landing();
	s->set_furthest_flight_landing(ROSS_MAX(s->get_furthest_flight_landing(), tw_now(lp)));
	ts = tw_rand_exponential(&lp->cur_state->rng, MEAN_LAND);
	e = tw_event_new(lp->gid, ts + s->get_furthest_flight_landing() - tw_now(lp), lp);
	m = (airport_message*)tw_event_data(e);
	m->type = LAND;
	m->waiting_time = s->get_furthest_flight_landing() - tw_now(lp);
	//s->furthest_flight_landing += ts;
    s->set_furthest_flight_landing(s->get_furthest_flight_landing() + ts);
	tw_event_send(e);
	break;
      }

    case DEPARTURE:
      {
	//s->planes_on_the_ground--;
    s->set_planes_on_the_ground(s->get_planes_on_the_ground() - 1);
	ts = tw_rand_exponential(&lp->cur_state->rng, mean_flight_time);
	rand_result = tw_rand_integer(&lp->cur_state->rng, 0, 3);
	dst_lp = 0;
	switch(rand_result)
	  {
	  case 0:
	      // Fly north
	      if(lp->gid < 32)
		  // Wrap around
		  dst_lp = lp->gid + 31 * 32;
	      else
		  dst_lp = lp->gid - 31;
	      break;
	  case 1:
	      // Fly south
	      if(lp->gid >= 31 * 32)
		  // Wrap around
		  dst_lp = lp->gid - 31 * 32;
	      else
		  dst_lp = lp->gid + 31;
	      break;
	  case 2:
	      // Fly east
	      if((lp->gid % 32) == 31)
		  // Wrap around
		  dst_lp = lp->gid - 31;
	      else
		  dst_lp = lp->gid + 1;
	      break;
	  case 3:
	      // Fly west
	      if((lp->gid % 32) == 0)
		  // Wrap around
		  dst_lp = lp->gid + 31;
	      else
		  dst_lp = lp->gid - 1;
	      break;
	  }

	e = tw_event_new(dst_lp, ts, lp);
	m = (airport_message*)tw_event_data(e);
	m->type = ARRIVAL;
	tw_event_send(e);
	break;
      }

    case LAND:
      {
	//s->landings++;
    s->set_landings(s->get_landings() + 1);
	//s->waiting_time += msg->waiting_time;
    s->set_waiting_time(s->get_waiting_time() + msg->waiting_time);

	e = tw_event_new(lp->gid, tw_rand_exponential(&lp->cur_state->rng, MEAN_DEPARTURE), lp);
	m = (airport_message*)tw_event_data(e);
	m->type = DEPARTURE;
	tw_event_send(e);
	break;
      }

    }
}

void
rc_event_handler(airport_state * s, tw_bf * bf, airport_message * msg, tw_lp * lp)
{
  switch(msg->type)
  {
    case ARRIVAL:
	s->set_furthest_flight_landing(msg->saved_furthest_flight_landing);
	tw_rand_reverse_unif(&lp->cur_state->rng);
	break;
    case DEPARTURE:
	tw_rand_reverse_unif(&lp->cur_state->rng);
	tw_rand_reverse_unif(&lp->cur_state->rng);
	break;
    case LAND:
	//s->landings--;
    s->set_landings(s->get_landings() - 1);
	//s->waiting_time -= msg->waiting_time;
    s->set_waiting_time(s->get_waiting_time() - msg->waiting_time);
	tw_rand_reverse_unif(&lp->cur_state->rng);
  }
}

void
final(airport_state * s, tw_lp * lp)
{
	wait_time_avg += ((s->get_waiting_time() / (double) s->get_landings()) / nlp_per_pe);
}

airport_state as;

tw_lptype airport_lps[] =
{
	{
		(init_f) init,
        (pre_run_f) NULL,
		(event_f) event_handler,
		(revent_f) rc_event_handler,
		(final_f) final,
		(map_f) mapping,
		sizeof(airport_state),
        &as,
	},
};

const tw_optdef app_opt [] =
{
	TWOPT_GROUP("Airport Model"),
    TWOPT_STIME("lookahead", lookahead, "lookahead for events"),
	//TWOPT_UINT("nairports", nlp_per_pe, "initial # of airports(LPs)"),
	TWOPT_UINT("nplanes", planes_per_airport, "initial # of planes per airport(events)"),
	TWOPT_STIME("mean", mean_flight_time, "mean flight time for planes"),
	TWOPT_UINT("memory", opt_mem, "optimistic memory"),
	TWOPT_END()
};

int
main(int argc, char **argv, char **env)
{
	int i;

    i = 0;
    while (i) {
        ;
    }

    printf("sizeof(airport_state) is %lu\n", sizeof(std::shared_ptr<int>));
	tw_opt_add(app_opt);
	tw_init(&argc, &argv);

	nlp_per_pe /= (tw_nnodes() * g_tw_npe);
	g_tw_events_per_pe =(planes_per_airport * nlp_per_pe / g_tw_npe) + opt_mem;

    g_tw_lookahead = lookahead;

	tw_define_lps(nlp_per_pe, sizeof(airport_message));

	for(i = 0; i < g_tw_nlp; i++)
		tw_lp_settype(i, &airport_lps[0]);

	tw_run();

	if(tw_ismaster())
	{
		printf("\nAirport Model Statistics:\n");
		printf("\t%-50s %11.4lf\n", "Average Waiting Time", wait_time_avg);
		printf("\t%-50s %11lld\n", "Number of airports",
			nlp_per_pe * g_tw_npe * tw_nnodes());
		printf("\t%-50s %11lld\n", "Number of planes",
			planes_per_airport * nlp_per_pe * g_tw_npe * tw_nnodes());
	}

	tw_end();

	return 0;
}
