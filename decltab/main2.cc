#include <iostream>
#include <cstdio>
#include <string>
#include <cassert>
#include <err.h>
#include <memory>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <map>
#include <limits>

#include "decltab.hh"

struct Event {
  enum Action {CHECK, DECLASSIFY, CLASSIFY} action;
  Addr base;
  unsigned size;
  auto operator<=>(const Event&) const = default;
};

int main(int argc, char *argv[]) {
  if (argc != 2)
    return EXIT_FAILURE;

  std::vector<Event> events;

  std::string line;
  while (std::getline(std::cin, line)) {
    char action_cstr[256];
    Addr base;
    unsigned size;
    if (std::sscanf(line.c_str(), "%*i: global: decltab-access: action=%s addr=0x%lx size=%u",
		    action_cstr, &base, &size) != 3)
      errx(EXIT_FAILURE, "invalid line: %s", line.c_str());
    static const std::map<std::string, Event::Action> action_strs = {
      {"check", Event::CHECK},
      {"declassify", Event::DECLASSIFY},
      {"classify", Event::CLASSIFY},
    };
    events.push_back({
	.action = action_strs.at(action_cstr),
	.base = base,
	.size = size,
      });
  }

  // First pass: in reverse, gather reuse distance information
  // Need to assign a reuse distanceto each entry.
  std::vector<unsigned> reuses;
  {
    std::map<Addr, size_t> live;
    size_t time = 0;
    for (auto event_it = events.rbegin(); event_it != events.rend(); ++event_it, ++time) {
      const Event& event = *event_it;
      unsigned reuse = (event.action == Event::DECLASSIFY) ? std::numeric_limits<unsigned>::max() : 0;
      for (Addr addr = event.base; addr < event.base + event.size; ++addr) {
	switch (event.action) {
	case Event::CHECK:
	  live[addr] = time;
	  break;
	case Event::DECLASSIFY:
	  {
	    const auto live_it = live.find(addr);
	    if (live_it != live.end()) {
	      reuse = std::min<unsigned>(reuse, time - live_it->second);
	      live.erase(live_it);
	    }
	  }
	  break;
	case Event::CLASSIFY:
	  live.erase(addr);
	  break;
	}
      }
      reuses.push_back(reuse);
    }
  }
  std::reverse(reuses.begin(), reuses.end());
  assert(reuses.size() == events.size());

  std::vector<bool> used;
  {
    auto event_it = events.begin();
    auto reuse_it = reuses.begin();
    used.resize(events.size());
    auto used_it = used.begin();
    for (; event_it != events.end(); ++event_it, ++reuse_it, ++used_it) {
      if (event_it->action == Event::DECLASSIFY) {
	assert(*reuse_it > 0);
	if (*reuse_it < std::numeric_limits<unsigned>::max())
	  *used_it = true;
      }
    }
  }


  // Now run actual experiment.
  size_t hits = 0;
  size_t misses = 0;
  {
    std::unique_ptr<DeclTab> decltab = std::make_unique<LRUByteDeclTab>(std::atoi(argv[1]));
    auto event_it = events.begin();
    auto used_it = used.begin();
    for (; event_it != events.end(); ++event_it, ++used_it) {
      switch (event_it->action) {
      case Event::CHECK:
	if (decltab->checkDeclassified(event_it->base, event_it->size)) {
	  ++hits;
	} else {
	  ++misses;
	}
	break;
      case Event::DECLASSIFY:
	if (*used_it)
	  decltab->setDeclassified(event_it->base, event_it->size);
	break;
      case Event::CLASSIFY:
	decltab->setClassified(event_it->base, event_it->size);
	break;
      }
    }
  }

  const double miss_rate = (double) misses / (double) (misses + hits) * 100;
  std::cout << "hits " << hits << "\n"
	    << "misses " << misses << "\n"
	    << "miss-rate " << std::setprecision(3) << miss_rate << "\n";  
}
