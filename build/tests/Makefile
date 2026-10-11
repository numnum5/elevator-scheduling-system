CXXFLAGS=-std=c++20 -pthread
TESTERS=test-call test-internal test-safety test-car-1 test-car-2 test-car-3 test-car-4 test-car-5 test-controller-1 test-controller-2 test-controller-3 test-controller-4 test-sched test-enqueue

testers: $(TESTERS)
test-enqueue: test-enqueue.cpp ../../src/server.cpp ../../src/tcp.cpp
	$(CXX) $(CXXFLAGS) -I../../include -o $@ test-enqueue.cpp ../../src/tcp.cpp
display-cars: display-cars.cpp
	$(CXX) $(CXXFLAGS) -o display-cars display-cars.cpp -lncurses -lm
clean:
	rm -f $(TESTERS) display-cars
.PHONY: testers clean
