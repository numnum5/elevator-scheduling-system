#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <queue>
#include <deque>
#include <list>
#include <memory>

// #include "logger.h"
#include "tcp.h"

enum class Status
{
    OPENING,
    OPEN,
    CLOSING,
    CLOSED,
    BETWEEN
};

enum class Direction {
    UP,
    DOWN,
    NONE
};

struct Floor {
    Direction direction;
    int32_t floor;
};

struct Car {
    int32_t min_floor;
    int32_t max_floor;
    int32_t current_floor;
    int32_t destination_floor;
    Status status;
    std::deque<Floor> floors;
    std::string name;
    int fd;
};

struct Connection
{
    int fd;
    // std::mutex* mutex;
    pthread_mutex_t * mutex;
    std::vector<std::unique_ptr<Car>>* cars;
};


Direction get_direction(int32_t source, int32_t destination) {

    // std::cerr << "get_direction(): " << source << " " << destination << std::endl;

    if (source > destination) {
        return Direction::DOWN;
    } else if (source < destination) {
        return Direction::UP;
    } else {
        return Direction::NONE;
    }
}

int32_t parse_floor(const std::string & floor)
{
    if (floor.starts_with("B")) {
        return -std::stoi(floor.substr(1));
    } 
    
    // std::cerr << "floor: " << floor << std::endl;

    return std::stoi(floor);
}

std::string direction(Direction direction) {
    if (direction == Direction::UP) {
        return "UP";
    }
    else {
        return "DOWN";
    }
}

void print_floors(const std::deque<Floor> & floors) {
    std::cerr << "Floors: \n";
    for (const auto & floor : floors) {

        std::cerr << std::format("Dir: {}, Floor: {}\n", direction(floor.direction), floor.floor);
    }
}

bool serviceable(int32_t source, int32_t destination, int32_t min, int32_t max) {
    if (source <= max && source >= min) {
        if (destination <= max && destination >= min) {
            return true;
        }
    }

    return false;
}


std::string status_to_string(Status status) {
    switch (status) {    
        case Status::CLOSING:
            return "CLOSING";
        case Status::CLOSED:
            return "CLOSED";
        case Status::OPEN:
            return "OPEN";
        case Status::OPENING:
            return "OPENING";
        case Status::BETWEEN:
            return "BETWEEN";
        default:
            return "";
    }
}

void handle_car_requests(std::string & message, Connection & connection)
{
    int sockfd = connection.fd;
    
    std::vector<std::unique_ptr<Car>> & cars = *(connection.cars);

    std::stringstream ss(message);
    std::string part;
    std::vector<std::string> result;

    // Split by the comma ',' character
    while (std::getline(ss, part, ' ')) {
        result.push_back(part);
    }
     
    if (result.size() != 4) {
        return;
    }       
    
    std::string name = result[1];
    int32_t min_floor = parse_floor(result[2]);
    int32_t max_floor = parse_floor(result[3]);

    std::unique_ptr<Car> newCar = std::make_unique<Car>();

    newCar->max_floor = max_floor;
    newCar->min_floor = min_floor;
    newCar->name = name;
    newCar->fd = sockfd;

    // yes even though we receive a status message right away, we still want to set some default values.
    newCar->status = Status::CLOSED;
    newCar->current_floor = min_floor;
    newCar->destination_floor = max_floor;

    Car* registered_car = newCar.get();

    pthread_mutex_lock(connection.mutex);
    cars.push_back(std::move(newCar));
    pthread_mutex_unlock(connection.mutex);

    std::string response;

    while (true) {
        int ret = receive_message(sockfd, response);

        if (ret != 0) {
            std::cerr << "Failed to receive message\n";
            return;
        }

        if(response == "INDIVIDUAL SERVICE" || response == "EMERGENCY") {
            break;
        }

        std::stringstream ss(response);
        std::string parts;
        std::vector<std::string> results;

        // Split by the comma ',' character
        while (std::getline(ss, parts, ' ')) {
            results.push_back(parts);
        }
        
        if (results.size() != 4) {
            return;
        }       

        // std::cerr << results[1] << std::endl;
    
        int status = std::stoi(results[1]);

        int32_t current_floor = parse_floor(results[2]);
        int32_t destination_floor = parse_floor(results[3]);

        std::cerr << "Recieved message: " 
        << std::format("STATUS {} {} {}", status_to_string(static_cast<Status>(status)), current_floor, destination_floor) << std::endl;
        // std::cerr << status << " " << current_floor << " "  << destination_floor << std::endl;

        pthread_mutex_lock(connection.mutex);
        registered_car->current_floor = current_floor;
        registered_car->destination_floor = destination_floor;
        registered_car->status = static_cast<Status>(status);

        // std::cerr << " " <<  registered_car->current_floor  << " "  << registered_car->destination_floor << std::endl;


        // std::cout << std::format("Status {}")


        if (registered_car->status == Status::OPENING) {
            std::cout << "STATUS OPENNING\n";

            if (! registered_car->floors.empty() && registered_car->floors.front().floor == current_floor) {
                registered_car->floors.pop_front();
            }

            if (! registered_car->floors.empty() && registered_car->floors.front().floor == current_floor) {
                registered_car->floors.pop_front();
            }
            
            if (! registered_car->floors.empty() && registered_car->floors.front().floor != destination_floor) {

            //      std::cerr << "car(): dest: " << registered_car->destination_floor << std::endl;
            // std::cerr << "front(): " << registered_car->floors.front().floor << std::endl;
                message = std::format("FLOOR {}", registered_car->floors.front().floor);

                    // std::cerr << message << std::endl;
                pthread_mutex_unlock(connection.mutex);
                send_message(sockfd, message);
                continue;
            }
        }
        // print_floors(registered_car->floors);

        pthread_mutex_unlock(connection.mutex);
    }

    pthread_mutex_lock(connection.mutex);
    std::erase_if(cars, [&](const std::unique_ptr<Car>& car) {
        return car->name == name;
    });
    pthread_mutex_unlock(connection.mutex);
}

int32_t increment_floor(int32_t current_floor) {
    if (current_floor == -1) {
        return 1;
    }

    return current_floor + 1;
}

int32_t decrement_floor(int32_t current_floor) {
    if (current_floor == 1) {
        return -1;
    }

    return current_floor - 1;
}

void insert_floors(std::deque<Floor> & floors, Floor source, Floor destination) {
    const auto contains_floor = [&](const Floor& requested) {
        return std::any_of(
            floors.begin(),
            floors.end(),
            [&](const Floor& queued) {
                return queued.direction == requested.direction &&
                       queued.floor == requested.floor;
            });
    };

    // const bool source_exists = contains_floor(source);
    // const bool destination_exists = contains_floor(destination);

    bool source_found = false;
    bool destination_found = false;

    for (std::size_t i = 0; i < floors.size(); ++i) {
        auto floor = floors[i];

        if (!source_found) { 
            // if (floor.direction == source.direction && floor.floor == source.floor) {
            //     source_found = true;
            // } else if 
            if (floor.direction == source.direction) {
                if (source.direction == Direction::UP) {   
                    // special case where its the virtual floor
                    if (floors[i].floor > source.floor) {

                        if (i != 0) {
                            floors.insert(floors.begin() + i, source);
                            source_found = true;
                            ++i; // Skip the inserted first value
                        }
                    } else if (floors[i].floor == source.floor) {
                        source_found = true;
                         ++i;
                    }
                } else { 
                    if (floors[i].floor < source.floor) {
                        if (i != 0) {
                            floors.insert(floors.begin() + i, source);
                            source_found = true;
                            ++i; // Skip the inserted first value
                        }
                    } else if (floors[i].floor == source.floor) {
                        source_found = true;
                         ++i;
                    }       
                }  
            }
        }

        if (source_found && !destination_found) {
            if (floor.direction == destination.direction) {
                if (destination.direction == Direction::UP) { 
                    if (floors[i].floor > destination.floor) {
                        floors.insert(floors.begin() + i, destination);
                        destination_found = true;
                        ++i; // Skip the inserted first value
                        break;
                    } else if (floors[i].floor == destination.floor) {
                        destination_found = true;
                        break;
                    }
                }
                else {
                    if (floors[i].floor < destination.floor) {
                        floors.insert(floors.begin() + i, destination);
                        destination_found = true;
                        ++i; // Skip the inserted first value
                        break;
                    } else if (floors[i].floor == destination.floor) {
                        destination_found = true;
                        break;
                    }
                }
            }
        }
    }


    if (!source_found) {
        floors.emplace_back(source);
    }


    if (!destination_found) {
        floors.emplace_back(destination);
    }
}

void enqueue_floors(std::deque<Floor> & floors, Floor source, Floor destination, Status status) {
    // floors_c.emplace_front(current_floor);
    // there 3 groups in elevator scheduling udu dud (down up down) or ud or du
    auto current_floor = floors.front();

    if (current_floor.direction == source.direction && current_floor.floor == source.floor) {
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
        

        switch (status) {    
            case Status::CLOSING:
                insert_floors(groups[2], source, destination);
                break;
            case Status::CLOSED:
            case Status::OPEN:
            case Status::OPENING:
            case Status::BETWEEN:
                insert_floors(groups[0], source, destination);
                break;
            default:
                break;
        }

        std::deque<Floor> merged;

        for (const auto& group : groups) {
            merged.insert(merged.end(), group.begin(), group.end());
        }

        // remove virtual floor

        merged.pop_front();

        std::cout << "called\n";

        floors = std::move(merged);
    } else {

        // std::cerr << "std\n";
        insert_floors(floors, source, destination);


        // print_floors(floors);
        // remove virtual floor
        floors.pop_front();

        // print_floors(floors);
    }
}


// void te() {

//     int left = 0;
//     int right = count - 1;
//     int middle;
//     while (left <= right)
//     {
//         middle = (left + right) / 2;
//         int comparsion = member.CompareTo(members[middle]);
//         // Returns out of the method if member exists
//         if(comparsion == 0) return;
//         else if(comparsion > 0) left = middle + 1;       
//         else right = middle - 1;
//     }

//     // Shifts the array right to accomodate the new member
//     int j = count - 1;
//     while(j >= left)
//     {
//     members[j + 1] = members[j];
//     j--;
//     }
//     members[left] = (Member) member;
//     count++;
        
    
// }


// this assumes that floor queue is in the same direction

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

Floor get_virtual_floor(Direction req_direction, Car * car);
void metric(Floor source, Car * car) {

    std::deque<Floor> floors = car->floors;
    floors.emplace_front(get_virtual_floor(source.direction, car));

    int points = 0;

    if (! floors.empty()) {
        auto first = floors.front();

        if (source.direction == first.direction) {
            if (source.direction == Direction::UP) {
                // first we check if we could place a virtual floor
                if (get_virtual_floor(source.direction, car).floor < source.floor) {

                }

            } else {

            }
        }
    } else {
        // 1 for being empty, cuz empty is better than wrong direction
        points++;
    }

}


Floor get_virtual_floor(Direction req_direction, Car * car) 
{
    Status status = car->status;
    int32_t dest = car->destination_floor; 
    int32_t curr = car->current_floor;
    const std::deque<Floor> & floors = car->floors;

    Floor current_floor;

    if (status == Status::BETWEEN) {
        // if its between usually its never the case that the floors will be empty
        Floor front = floors.front();

        // if floor being requested is the same floor as the first element of the queue
        if (front.floor ==  dest) {
            current_floor = front;
        } else {
            auto dir = get_direction(curr, dest);

            current_floor = 
                Floor{
                    dir, 
                    dir == Direction::UP 
                        ? increment_floor(curr) 
                        : decrement_floor(curr)
                    };
        }
    } else {
        Direction curr_floor_dir;
        
        if (floors.empty()) {
            curr_floor_dir = req_direction;
        } else {
            Floor first = floors.front();

            if (first.floor == curr) {
                curr_floor_dir = first.direction;
            } else {
                curr_floor_dir = get_direction(curr, first.floor);
            }
        }

        current_floor = {curr_floor_dir, curr};
    }

    return current_floor;
}

void handle_call_requests(std::string & message, Connection & connection)
{
    int sockfd = connection.fd;
    std::vector<std::unique_ptr<Car>> & cars = *(connection.cars);

    std::stringstream ss(message);
    std::string part;
    std::vector<std::string> result;

    while (std::getline(ss, part, ' ')) {
        result.push_back(part);
    }

    if (result.size() != 3) {
        return;
    }

    // std::cerr << result.size() << std::endl;

    // const std::string & source_floor = result[1];
    int32_t source_floor = parse_floor(result[1]);
    int32_t destination_floor = parse_floor(result[2]);
    // const std::string & destination_floor = result[2];

    // std::cerr << destination_floor << source_floor << std::

    // Logger::info("test");
    std::cerr << std::format("CALL req: source: {} destination: {}\n", source_floor, destination_floor);

    Car* selected_car = nullptr;


    pthread_mutex_lock(connection.mutex);



    // std::vector<Car*> matching;

    // for (const std::unique_ptr<Car>& car: cars) {
    //     if (car && serviceable(source_floor, destination_floor, car->min_floor, car->max_floor)) {
    //         matching.push_back(car.get());
    //     }
    // }



    // std::sort(matching.begin(), matching.end(),
    //     [](const Car* a, const Car* b) {
    //         int distance_a 
    //         if (! a->floors.empty()) {


    //         }

    //         return ;
    //     });


    auto it = std::find_if(cars.begin(), cars.end(),
        [&](const std::unique_ptr<Car>& car) {
            // for each car we want to first find the best fit that is first go over the cars at least once
            // o(n) search 
            // the algorithm first we have to find the candidates 
            // my thought is that we use the closest elevator metric,
            // e.g. if the call was from floor 4 to 7, and we have elevators at floor 5, 6, 7, we would use
            // use the one that is the closet to floor 4 which is 5, but of course this depends entirely on the direction
            // that the elevator is currently going
            // if elevator has no floors in queue then it can be assigned
            // but if it has direction say 6 UP and 7 DOWN respectively for elevator 1 and 2
            // since 4 to 7 is going UP and 6 is going up and it is higher than 4 it wont be able to service as qucikly
            // 7 on the other hand if its 

            return serviceable(source_floor, destination_floor, car->min_floor, car->max_floor);
        });
        
    if (it != cars.end()) {
        selected_car = it->get();
        std::cout << std::format("call check status: {} curr: {} front: {} dest: {}\n", status_to_string(selected_car->status), selected_car->current_floor, selected_car->floors.front().floor, selected_car->destination_floor);
    }


    pthread_mutex_unlock(connection.mutex);

    if (selected_car == nullptr) {
        std::string message = "UNAVAILABLE";
        send_message(sockfd, message);
    } else {
        const Direction direction = get_direction(source_floor, destination_floor);

        Floor request_source{direction, source_floor};
        Floor request_destination{direction, destination_floor};

        pthread_mutex_lock(connection.mutex);
        std::string message = std::format("CAR {}", selected_car->name);
        pthread_mutex_unlock(connection.mutex);

        send_message(connection.fd, message);
    
        pthread_mutex_lock(connection.mutex);

        Floor current_floor = get_virtual_floor(direction, selected_car);
        std::cout << std::format("call check status: {} curr: {} front: {} dest: {}\n", status_to_string(selected_car->status), selected_car->current_floor, selected_car->floors.front().floor, selected_car->destination_floor);

        std::deque<Floor> & floors_c = selected_car->floors;

        floors_c.emplace_front(current_floor);
        enqueue_floors(floors_c, request_source, request_destination, selected_car->status);
        print_floors(floors_c);
        int32_t front_floor = selected_car->floors.front().floor;
//         std::cerr << "stat: \n";
//         std::cerr <<  selected_car->name << std::endl;
//         std::cerr << "currentfloor: "<<selected_car->current_floor << std::endl;
//         std::cerr <<  "call destination" << request_destination.floor << std::endl;
//         std::cerr <<  "selected car front queue front: " << front_floor << std::endl;
// std::cerr <<  "request_source.floor: " << request_source.floor << std::endl;
        // print_floors(selected_car->floors);

        // std::cerr << "ismempty:" << selected_car->floors.empty() << std::endl;
        // std::cerr << "selected_car->current_floor: " << selected_car->current_floor << std::endl;
        // std::cerr << "request_destination.floor: " << request_destination.floor << std::endl;
        // std::cerr << "front_floor: " << front_floor << std::endl;


        // std::cerr << (request_destination.floor != front_floor) <<  (selected_car->current_floor == front_floor) << std::endl;
        
        // essentially if the elevator is already on the same floor as the call pad rquest e..g user pressed the elevator button
        // on the same floor a
        if ( selected_car->current_floor == request_source.floor) {
        //  std::cerr << "sending emssage \n";
            std::cerr << "sending emssage sour\n";
            
            message =  std::format("FLOOR {}", request_source.floor);
            int fd = selected_car->fd;
            pthread_mutex_unlock(connection.mutex);
            send_message(fd, message);
            return;
        }
        
        if (selected_car->destination_floor != front_floor) {            
            message =  std::format("FLOOR {}", selected_car->floors.front().floor);
            std::cerr << "sending floor message because the destination is different: " << message << std::endl;

            int fd = selected_car->fd;
            pthread_mutex_unlock(connection.mutex);
            send_message(fd, message);
            return;
        }

        pthread_mutex_unlock(connection.mutex);
    }
    // const std::string & cu
    // std::cerr << result.at(0);
}

void  * handler(void * arg) {
    std::unique_ptr<Connection> connection{
        static_cast<Connection*>(arg)
    };
    // std::cerr << "THREAD\n";

    int fd = connection->fd;
    std::string message;

    int ret = receive_message(fd, message);

    if (ret != 0) {
        close(fd);
        return nullptr;
    }

    const std::string message_type = message.substr(0, 4);


    // std::cerr << message << std::endl;
    // std::cerr << message_type << std::endl;
    if (message_type == "CALL") {
        handle_call_requests(message, *connection);
    }

    else if (message_type == "CAR "){
        handle_car_requests(message, *connection);
    }

    close(fd);
    return nullptr;
}









int index2(std::deque<Floor> & floors, Floor source) {
    std::size_t i = 0;
    std::vector<std::deque<Floor>> groups(3);
    // there is always going to be a virtual floor regradless of whatever;
    Direction dir = floors.front().direction;
    int group_idx = 0;
    int number_of_blocks = 1;

    for (std::size_t i = 0; i < floors.size(); ++i) {
        auto floor = floors[i];

        if (floor.direction != dir) {
            dir = floor.direction;
            number_of_blocks++;
            group_idx++;

            if (static_cast<std::size_t>(group_idx) >= groups.size()) {
                groups.emplace_back();
            }
        }

        groups[group_idx].emplace_back(floor);
    }


    // lets count the the number of blocks


    // special case
    if (number_of_blocks == 1) {



        // find an index where floors go down


        

        // inserting 5 to 2
        // what uu means, we have 3 5 7 2 7
        if (dir != source.direction) {
            if (dir == Direction::UP) {

                for (std::size_t i = 0; i < floors.size(); i++) {
                
                }


            
                // means source is down lets see if we can put
                // start interating from the second element in the list (non virtual floor)
                for (std::size_t i = 0; i < floors.size(); i++) {
                    auto virt = floors[i];
                    if (virt.floor <= source.floor) {
                        if (i != 0) {

                        }
                    }
                }
            }
        }
    } 

    std::cout << group_idx << std::endl;


    // so the possible comibinations are 
    // udu, dud, dd, uu, ud, du








    // // const bool source_exists = contains_floor(source);
    // // const bool destination_exists = contains_floor(destination);
    // for (; i < floors.size(); ++i) {
    //     auto floor = floors[i];
    //         if (floor.direction == source.direction) {
    //             if (source.direction == Direction::UP) {   
    //                 // special case where its the virtual floor
    //                 if (floors[i].floor > source.floor) {

    //                     if (i != 0) {
    //                         return i;
    //                     }
    //                 } else if (floors[i].floor == source.floor) {
    //                     source_found = true;
    //                      ++i;
    //                 }
    //             } else { 
    //                 if (floors[i].floor < source.floor) {
    //                     if (i != 0) {
    //                         floors.insert(floors.begin() + i, source);
    //                         // source_found = true;
    //                         ++i; // Skip the inserted first value
    //                     }
    //                 } else if (floors[i].floor == source.floor) {
    //                     // source_found = true;
    //                      ++i;
    //                 }       
    //             }  
    //         }
        

    //     // if (source_found && !destination_found) {
    //     //     if (floor.direction == destination.direction) {
    //     //         if (destination.direction == Direction::UP) { 
    //     //             if (floors[i].floor > destination.floor) {
    //     //                 floors.insert(floors.begin() + i, destination);
    //     //                 destination_found = true;
    //     //                 ++i; // Skip the inserted first value
    //     //                 break;
    //     //             } else if (floors[i].floor == destination.floor) {
    //     //                 destination_found = true;
    //     //                 break;
    //     //             }
    //     //         }
    //     //         else {
    //     //             if (floors[i].floor < destination.floor) {
    //     //                 floors.insert(floors.begin() + i, destination);
    //     //                 destination_found = true;
    //     //                 ++i; // Skip the inserted first value
    //     //                 break;
    //     //             } else if (floors[i].floor == destination.floor) {
    //     //                 destination_found = true;
    //     //                 break;
    //     //             }
    //     //         }
    //     //     }
    //     // }
    // }


    return 10;
}





int main(void) {

    std::deque<Floor> route{
        {Direction::UP, 3},
        {Direction::UP, 4},
        {Direction::UP, 5}, // virtual floor and request source
        {Direction::UP, 7},

        {Direction::UP, 2}, // virtual floor and request source
        {Direction::UP, 7},
        // 8 uup would go here
        // {Direction::DOWN, 6},
        // {Direction::DOWN, 2},

    };

    Floor source {
        Direction::DOWN,
        6
    };


    // int index = binary_search(source, route);
    index2(route, source);
    // int index = find_index(source, route);

    // std::cout << index << std::endl;

    return -1;


    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::perror("socket");
        return 1;
    }

    // Allow restarting the server without waiting for old TCP connections.
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1) {
        std::perror("setsockopt");
        close(server_fd);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(3000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd,
             reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) == -1) {
        std::perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        std::perror("listen");
        close(server_fd);
        return 1;
    }

    pthread_mutex_t mutex;

    pthread_mutex_init(&mutex, nullptr);

    // std::cerr << "Listening on 127.0.0.1:3000\n";

    std::vector<std::unique_ptr<Car>> cars;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::perror("accept");
            close(server_fd);            
            return 1;
        }

        auto* connection = new Connection{client_fd, &mutex, &cars};
        pthread_t thread;

        int error = pthread_create(&thread, nullptr, handler, reinterpret_cast<void *>(connection));

        if (error != 0) {
            fprintf(stderr, "pthread_create error: %d\n", error);
            delete connection;
            close(client_fd);
            continue;
        }

        // Release thread resources automatically when it finishes.
        pthread_detach(thread);
    }
}
