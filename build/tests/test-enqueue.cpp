#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <string_view>

// Pull in the real scheduling implementation while renaming the controller's
// entry point so this test can provide its own main().
#define main controller_main
#include "../../src/server.cpp"
#undef main

struct ExpectedFloor {
    Direction direction;
    int32_t floor;
};

bool expect_route(
    std::string_view test_name,
    const std::deque<Floor>& actual,
    std::initializer_list<ExpectedFloor> expected)
{
    bool matches = actual.size() == expected.size();
    auto actual_it = actual.begin();

    for (auto expected_it = expected.begin();
         matches && expected_it != expected.end();
         ++expected_it, ++actual_it) {
        matches = actual_it->direction == expected_it->direction &&
                  actual_it->floor == expected_it->floor;
    }

    if (matches) {
        std::cerr << "PASS: " << test_name << '\n';
        return true;
    }

    std::cerr << "FAIL: " << test_name << "\nExpected:\n";
    for (const ExpectedFloor& floor : expected) {
        std::cerr << "  " << direction(floor.direction)
                  << ' ' << floor.floor << '\n';
    }

    std::cerr << "Actual:\n";
    for (const Floor& floor : actual) {
        std::cerr << "  " << direction(floor.direction)
                  << ' ' << floor.floor << '\n';
    }

    return false;
}

int binary_search(Floor source, std::deque<Floor> & floors) {
    int left = 0;
    int right = floors.size() - 1;
    int middle;
    while (left <= right)
    {
        middle = (left + right) / 2;

        if (source.floor > floors[middle].floor) {
            left = middle + 1;
        } else if (source.floor < floors[middle].floor) {
            right = middle - 1;
        } else {
            return middle;
        }
    }

    return left;
}

int find_index(Floor source, std::deque<Floor> & floors, Status status) {
    std::vector<std::deque<Floor>> groups(3);
    // there is always going to be a virtual floor regradless of whatever;
    Direction dir = floors.front().direction;
    int group_idx = 0;
    for (std::size_t i = 0; i < floors.size(); ++i) {
        auto floor = floors[i];

        if (floor.direction != dir) {
            dir = floor.direction;
            group_idx++;

            if (static_cast<std::size_t>(group_idx) >= groups.size()) {
                groups.emplace_back();
            }
        }

        groups[group_idx].emplace_back(floor);
    }
    int index = floors.size() - 1;
    // check first group
    if (! groups[0].empty() && groups[0].front().direction == source.direction) {

        // we are checking the virtual floor
        if (status == Status::CLOSED) {
            index = binary_search(source, groups[2]);
        } else {
            index = binary_search(source, groups[0]);
        }
    // check second group
    } else {

    }

    return -1;
}


int main()
{
    int failures = 0;

    {
        // The first entry is the temporary current/virtual floor. It should be
        // removed, leaving the pickup and destination in ascending order.
        std::deque<Floor> route{{Direction::UP, 1}};
        enqueue_floors(
            route,
            {Direction::UP, 3},
            {Direction::UP, 6},
            Status::CLOSED);

        failures += !expect_route("empty upward route", route, {
            {Direction::UP, 3},
            {Direction::UP, 6},
        });
    }

    {
        // Crossing from above ground into the basement must not create floor
        // zero. In the internal representation, B1 is -1 and B3 is -3.
        std::deque<Floor> route{{Direction::DOWN, 1}};
        enqueue_floors(
            route,
            {Direction::DOWN, -1},
            {Direction::DOWN, -3},
            Status::CLOSED);

        failures += !expect_route("basement route skips floor zero", route, {
            {Direction::DOWN, -1},
            {Direction::DOWN, -3},
        });
    }

    {
        // Equivalent to the middle of controller test 4. The new downward
        // pickup belongs before the existing downward stops.
        std::deque<Floor> route{
            {Direction::UP, 5}, // virtual floor
            {Direction::UP, 6},
            {Direction::DOWN, 7},
            {Direction::DOWN, 4},
        };
        enqueue_floors(
            route,
            {Direction::DOWN, 8},
            {Direction::DOWN, 4},
            Status::BETWEEN);

        failures += !expect_route("insert into later downward sweep", route, {
            {Direction::UP, 6},
            {Direction::DOWN, 8},
            {Direction::DOWN, 7},
            {Direction::DOWN, 4},
        });
    }

    {
        // Repeating an identical request in an ordered direction group must
        // not add another copy of either stop.
        std::deque<Floor> route{
            {Direction::UP, 1}, // virtual floor
            {Direction::UP, 3},
            {Direction::UP, 6},
        };
        enqueue_floors(
            route,
            {Direction::UP, 3},
            {Direction::UP, 6},
            Status::BETWEEN);

        failures += !expect_route("identical request is deduplicated", route, {
            {Direction::UP, 3},
            {Direction::UP, 6},
        });
    }

    {
        // Regression: if a same-direction group is already out of order, the
        // insertion loop can insert source 3 before it reaches the existing 3.
        // The request already exists, so the desired result is unchanged after
        // removal of the virtual floor. This currently exposes the duplicate.
        std::deque<Floor> route{
            {Direction::DOWN, 5}, // virtual floor
            {Direction::DOWN, 4},
            {Direction::DOWN, 2},
            {Direction::DOWN, 3},
            {Direction::DOWN, 1},
        };
        enqueue_floors(
            route,
            {Direction::DOWN, 3},
            {Direction::DOWN, 2},
            Status::BETWEEN);

        failures += !expect_route("out-of-order route does not duplicate source", route, {
            {Direction::DOWN, 4},
            {Direction::DOWN, 2},
            {Direction::DOWN, 3},
            {Direction::DOWN, 1},
        });
    }

    {
        // This catches a direction-boundary bug where the first floor of each
        // new group used to be discarded. It also verifies dynamic grouping
        // beyond the original fixed array of three groups.
        std::deque<Floor> route{
            {Direction::UP, 1}, // virtual floor and request source
            {Direction::UP, 3},
            {Direction::DOWN, 2},
            {Direction::UP, 4},
            {Direction::DOWN, 1},
        };
        enqueue_floors(
            route,
            {Direction::UP, 1},
            {Direction::UP, 6},
            Status::CLOSED);

        failures += !expect_route("preserve every direction group", route, {
            {Direction::UP, 3},
            {Direction::UP, 6},
            {Direction::DOWN, 2},
            {Direction::UP, 4},
            {Direction::DOWN, 1},
        });
    }

    {
        // CLOSING must insert only into its selected group; it must not fall
        // through and insert the request into group zero as well.
        std::deque<Floor> route{{Direction::UP, 1}};
        enqueue_floors(
            route,
            {Direction::UP, 1},
            {Direction::UP, 3},
            Status::CLOSING);

        failures += !expect_route("closing status does not fall through", route, {
            {Direction::UP, 1},
            {Direction::UP, 3},
        });
    }

    {
        // With an up-down-up route, CLOSING requests belong to the final up
        // block and must leave the first two blocks unchanged.
        std::deque<Floor> route{
            {Direction::UP, 5}, // virtual floor and request source
            {Direction::UP, 7},
            {Direction::DOWN, 6},
            {Direction::DOWN, 2},
            {Direction::UP, 3},
            {Direction::UP, 4},
        };
        enqueue_floors(
            route,
            {Direction::UP, 5},
            {Direction::UP, 8},
            Status::CLOSING);

        failures += !expect_route("closing request in UDU blocks", route, {
            {Direction::UP, 7},
            {Direction::DOWN, 6},
            {Direction::DOWN, 2},
            {Direction::UP, 3},
            {Direction::UP, 4},
            {Direction::UP, 5},
            {Direction::UP, 8},
        });
    }

    {
        // The mirrored down-up-down configuration must likewise place a
        // CLOSING request in the final down block in descending order.
        std::deque<Floor> route{
            {Direction::DOWN, 5}, // virtual floor and request source
            {Direction::DOWN, 2},
            {Direction::UP, 3},
            {Direction::UP, 7},
            {Direction::DOWN, 6},
            {Direction::DOWN, 4},
        };
        enqueue_floors(
            route,
            {Direction::DOWN, 5},
            {Direction::DOWN, 1},
            Status::CLOSING);

        failures += !expect_route("closing request in DUD blocks", route, {
            {Direction::DOWN, 2},
            {Direction::UP, 3},
            {Direction::UP, 7},
            {Direction::DOWN, 6},
            {Direction::DOWN, 5},
            {Direction::DOWN, 4},
            {Direction::DOWN, 1},
        });
    }

    if (failures != 0) {
        std::cerr << failures << " enqueue test(s) failed\n";
        return 1;
    }

    std::cerr << "All enqueue tests passed\n";
    return 0;
}
