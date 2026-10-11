#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <stdint.h>
#include <iostream>
#include <cctype>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <semaphore.h>
#include <thread>
#include <chrono>
#include <iostream>
#include "tcp.h"

enum class Status
{
    OPENING,
    OPEN,
    CLOSING,
    CLOSED,
    BETWEEN
};

struct car_shared_mem {
  pthread_mutex_t mutex;           // Locked while accessing struct contents
  pthread_cond_t cond;             // Signalled when the contents change
    int32_t current_floor;
    int32_t destination_floor;
    Status status;
  uint8_t open_button;             // 1 if open doors button is pressed, else 0
  uint8_t close_button;            // 1 if close doors button is pressed, else 0
  uint8_t door_obstruction;        // 1 if obstruction detected, else 0
  uint8_t overload;                // 1 if overload detected
  uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
  uint8_t individual_service_mode; // 1 if in individual service mode, else 0
  uint8_t emergency_mode;          // 1 if in emergency mode, else 0
};

struct context_t {
    car_shared_mem * shared_mem;

    int32_t max_floor;
    int32_t min_floor;
    int sockfd;
    int delay;
};


void sleep(pthread_mutex_t* mutex, int milliseconds) {
    pthread_mutex_unlock(mutex);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    pthread_mutex_lock(mutex);
}


class Car {
    public:
        int32_t current_floor;
        int32_t destination_floor;
        Status status;
        uint8_t open_button;             // 1 if open doors button is pressed, else 0
        uint8_t close_button;     
        uint8_t individual_service_mode;
        
        Car(int32_t current, int32_t destination, Status status, uint8_t individual_service_mode, uint8_t close_button, uint8_t open_button)
            : current_floor(current),
            destination_floor(destination),
            status(status),
            close_button(close_button),
            open_button(open_button),
            individual_service_mode(individual_service_mode)
        {
        }

        bool operator==(const Car& other) const {
            return 
                    current_floor == other.current_floor &&
                    destination_floor == other.destination_floor &&
                    status == other.status &&
                    individual_service_mode == other.individual_service_mode &&
                    close_button == other.close_button && 
                    open_button == other.open_button;
        }


        void print_content() const {
            std::cout << "Current floor: " << current_floor << ", Destination floor: " << destination_floor
                      << ", Status: " << static_cast<int>(status) << ", Individual Service Mode: " << static_cast<int>(individual_service_mode) << std::endl;

        }
};


static void * handle_individual_service_mode(void * arg);
static void* create_shared_object(const std::string& name);
static void * handle_receive(void * arg);
static void * handle_normal_operation(void * arg);

static void elevator_state_machine(car_shared_mem * shm, int delay) ;
void initalise_shared_mem(car_shared_mem * shm, int32_t lowest, int32_t highest)
{
    shm->close_button = 0;
    shm->open_button = 0;
    shm->door_obstruction = 0;
    shm->overload = 0;
    shm->emergency_stop = 0;
    shm->individual_service_mode = 0;
    shm->emergency_mode = 0;
    shm->status = Status::CLOSED;
    shm->current_floor = lowest;
    shm->destination_floor = lowest; 
}

int32_t parse_floor(const std::string & floor)
{
    if (floor.starts_with("B")) {
        return -std::stoi(floor.substr(1));
    } 
    
    return std::stoi(floor);
}


bool status_changed(const Car & previous, const Car & changed, int ret) {

    if (previous.destination_floor != changed.destination_floor 
        || previous.status != changed.status 
        || previous.current_floor != changed.current_floor || ret == ETIMEDOUT) {

        return true;
    }

    return false;
}


void print_shm(car_shared_mem * shm) {
    std::cout << "Current floor: " << shm->current_floor << ", Destination floor: " << shm->destination_floor
              << ", Status: " << static_cast<int>(shm->status) << ", Individual Service Mode: " << static_cast<int>(shm->individual_service_mode) << std::endl;

}

int main(int argc, char * argv[]) {
    // Check argument format
    if(argc != 5){
        std::cerr << "./car {name} {lowest floor} {highest floor} {delay}" << std::endl;
        return 1;
    }    

    const std::string name = argv[1];
    const std::string lowestFloor = argv[2];
    const std::string highestFloor = argv[3];
    const std::string delayStr = argv[4];

    int highest = parse_floor(highestFloor);
    int lowest = parse_floor(lowestFloor);

    // std::cout << highest << " " << lowest << std::endl;

    const std::string shared_mem_name = "/car" + name; 
    int delay = std::stoi(delayStr);

    car_shared_mem * shared_mem =  reinterpret_cast<car_shared_mem *>(create_shared_object(shared_mem_name));  

    if (shared_mem == nullptr) {
        std::cerr << "Failed to create shared memory" << std::endl;
        return 1;
    }

    initalise_shared_mem(shared_mem, lowest, highest);

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    
    pthread_mutexattr_init(&mutex_attr);
    pthread_condattr_init(&cond_attr);
    
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED); // Set for shared use
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED); // Set for shared use

    pthread_mutex_init(&(shared_mem->mutex), &mutex_attr);
    pthread_cond_init(&(shared_mem->cond), &cond_attr);

    pthread_t individual_service_thread;
    pthread_t normal_operation_thread;

    // print_shm(shared_mem);

    context_t thread_context{shared_mem, highest, lowest, -1, delay};

    int ret = pthread_create(&normal_operation_thread, nullptr, handle_normal_operation, reinterpret_cast<void *>(&thread_context));
    
    if (ret != 0) {
        std::cerr << "Failed to create normal operation thread" << std::endl;
        return 1;
    }
    
    ret = pthread_create(&individual_service_thread, nullptr, handle_individual_service_mode, reinterpret_cast<void *>(&thread_context));

    if (ret != 0) {
        std::cerr << "Failed to create individual service thread" << std::endl;
        return 1;
    }

    // Initialize socket address structure for communication with controller
    // let main thread handle connection atempts
    pthread_t receive_thread;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);

    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        return 1;
    }

    // if counter == 0 continue executing
    // after it becomes exit loop
    while(true) {
        // Wait for the elevator to go out of individual service mode and emergency mode before reconnecting
        pthread_mutex_lock(&shared_mem->mutex);  

        while (shared_mem->individual_service_mode || shared_mem->emergency_mode) {
            pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        }

        pthread_mutex_unlock(&shared_mem->mutex); 

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);

        if(sockfd == -1) {
            close(sockfd);
            continue;
        }

        if(connect(sockfd, reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            context_t context{shared_mem, highest, lowest, sockfd, delay};

            int error = pthread_create(&receive_thread, nullptr, handle_receive, &context);

            if (error != 0) {
                std::cerr << "THread creation failed exiting\n";
                return 1;
            }

            std::string message = std::format("CAR {} {} {}", name, lowestFloor, highestFloor);

            // send first message to register the car
            send_message(sockfd, message);

            // Store previous state (used for tracking any changes in the shared mem)
            pthread_mutex_lock(&shared_mem->mutex);
            Car previous{shared_mem->current_floor, shared_mem->destination_floor, shared_mem->status, shared_mem->individual_service_mode, shared_mem->close_button, shared_mem->open_button};
            message = std::format("STATUS {} {} {}", static_cast<int>(shared_mem->status), shared_mem->current_floor, shared_mem->destination_floor);
            pthread_mutex_unlock(&shared_mem->mutex);

            // send first status message immediately after registeration
            send_message(sockfd, message);
            
            while(true)
            {
                // Default condition variables use CLOCK_REALTIME.
                timespec deadline{};
                clock_gettime(CLOCK_REALTIME, &deadline);

                deadline.tv_nsec += 500'000'000; // Add 500 ms

                if (deadline.tv_nsec >= 1'000'000'000) {
                    ++deadline.tv_sec;
                    deadline.tv_nsec -= 1'000'000'000;
                }

                pthread_mutex_lock(&shared_mem->mutex);

                Car changed{shared_mem->current_floor, shared_mem->destination_floor, shared_mem->status, shared_mem->individual_service_mode, shared_mem->close_button, shared_mem->open_button};

                int ret;

                while (previous == changed) {
                    ret = pthread_cond_timedwait(&shared_mem->cond, &shared_mem->mutex, &deadline);
                    // timedwait reacquires the mutex before returning.
                    changed = Car{shared_mem->current_floor, shared_mem->destination_floor, shared_mem->status, shared_mem->individual_service_mode, shared_mem->close_button, shared_mem->open_button};

                    if (ret == ETIMEDOUT) {
                        break; // Send heartbeat even if unchanged.
                    }
                }
                // std::cout << "Main thread: Detected change in shared memory" << std::endl;

                if (shared_mem->individual_service_mode) {
                    message = "INDIVIDUAL SERVICE";
                    pthread_mutex_unlock(&shared_mem->mutex);
                    send_message(sockfd, message);
                    break;
                }

                if (shared_mem->emergency_mode) {
                    message = "EMERGENCY";
                    pthread_mutex_unlock(&shared_mem->mutex);
                    send_message(sockfd, message);
                    break;
                }


                if (status_changed(previous, changed, ret)) {
                    message = std::format("STATUS {} {} {}", static_cast<int>(shared_mem->status), shared_mem->current_floor, shared_mem->destination_floor);

                    previous = Car{
                        shared_mem->current_floor,
                        shared_mem->destination_floor,
                        shared_mem->status,
                        shared_mem->individual_service_mode,
                        shared_mem->close_button,
                        shared_mem->open_button
                    };

                    if (shared_mem->status == Status::OPENING) {
                        std::cout << "Status change to opening\n";
                    }

                    pthread_mutex_unlock(&shared_mem->mutex);

                    send_message(sockfd, message);

                    continue;
                }

                previous = Car{
                    shared_mem->current_floor,
                    shared_mem->destination_floor,
                    shared_mem->status,
                    shared_mem->individual_service_mode,
                    shared_mem->close_button,
                    shared_mem->open_button
                };

                pthread_mutex_unlock(&shared_mem->mutex);
            }

            close(sockfd);
            shutdown(sockfd, SHUT_RDWR);
            pthread_join(receive_thread, nullptr);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    pthread_join(normal_operation_thread, nullptr);
    pthread_join(individual_service_thread, nullptr);

    // pthread_join(thread, NULL);

    return 0;
}

static void* create_shared_object(const std::string& name) {
    const char* share_name = name.c_str();

    if (shm_unlink(share_name) == -1 && errno != ENOENT) {
        std::perror("shm_unlink");
        return nullptr;
    }

    int fd = shm_open(share_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        std::perror("shm_open");
        return nullptr;
    }

    if (ftruncate(fd, sizeof(car_shared_mem)) == -1) {
        std::perror("ftruncate");
        close(fd);
        shm_unlink(share_name);
        return nullptr;
    }

    void* ptr = mmap(nullptr, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (ptr == MAP_FAILED) {
        std::perror("mmap");
        close(fd);
        shm_unlink(share_name);
        return nullptr;
    }

    close(fd);
    return ptr;
}


enum class WaitResult {
    Changed,
    TimedOut,
    Error
};

// The condition variable must use CLOCK_REALTIME.
template <typename Predicate>
WaitResult delay_ms(
    pthread_cond_t* cond,
    pthread_mutex_t* mutex,
    int ms,
    Predicate changed
) {
    if (ms < 0) {
        return WaitResult::Error;
    }

    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        return WaitResult::Error;
    }

    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (ms % 1000) * 1'000'000L;

    if (deadline.tv_nsec >= 1'000'000'000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1'000'000'000L;
    }

    while (!changed()) {
        int ret = pthread_cond_timedwait(cond, mutex, &deadline);

        if (ret == ETIMEDOUT) {
            // State could have changed at the timeout boundary.
            return changed() ? WaitResult::Changed : WaitResult::TimedOut;
        }

        if (ret != 0) {
            return WaitResult::Error;
        }
    }

    return WaitResult::Changed;
}

static void * handle_individual_service_mode(void * arg) {
    context_t * context = reinterpret_cast<context_t *>(arg);
    car_shared_mem * shm = context->shared_mem;
    int delay = context->delay;

    pthread_mutex_lock(&shm->mutex);  
    Car previous(shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button);
    pthread_mutex_unlock(&shm->mutex);  

    while(true) { 
        pthread_mutex_lock(&shm->mutex);  

        Car changed{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        // Wait for change in destination floor and status 

        // We have to wait for floor change, we also need to wait if its the first time its being indivudial service mode
        while (changed == previous) {
            pthread_cond_wait(&shm->cond, &shm->mutex);
            changed = Car{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        }

        if (shm->individual_service_mode && previous.individual_service_mode == 0) {
            switch (shm->status) {
                case Status::BETWEEN:
                case Status::CLOSING:
                case Status::OPENING:
                    previous = changed;
                    pthread_mutex_unlock(&shm->mutex);
                    continue;
                default:
                    break;
            }

            if (previous.status == Status::BETWEEN) {
                shm->status = Status::CLOSED;
                shm->destination_floor = shm->current_floor;
                pthread_cond_broadcast(&shm->cond);
            }

            previous = changed;
        }
        
        if (shm->individual_service_mode) {

            // std::cout << "Open button: " << static_cast<int>(shm->open_button)  << std::endl;

            if (shm->current_floor != shm->destination_floor) {

                // std::cout << "floor different\n";

                if (shm->current_floor == context->max_floor && shm->destination_floor > context->max_floor) {
                    shm->destination_floor = context->max_floor;
                } else if (shm->current_floor == context->min_floor && shm->destination_floor < context->min_floor) {
                    shm->destination_floor = context->min_floor;
                } else {
                    shm->status = Status::BETWEEN;
                    pthread_cond_broadcast(&shm->cond);

                    // std::cout << "Delay: " << delay << std::endl; 
                    // you think users can press open and clsoe but internals.c wont allow that so its okay
                    sleep(&shm->mutex, delay);

                    shm->status = Status::CLOSED;
                    shm->current_floor = shm->destination_floor;
                    pthread_cond_broadcast(&shm->cond);
                }
            }

            

            // obviously we are assuming these two operations are exclusive and cannot be pressed at the same time, if they are then we will just ignore the open button
            if (shm->open_button) {
                shm->open_button = 0;
                shm->status = Status::OPENING;
                pthread_cond_broadcast(&shm->cond);

                shm->status = Status::OPEN;
                pthread_cond_broadcast(&shm->cond);

            } else if (shm->close_button) {
                shm->close_button = 0;
                shm->status = Status::CLOSING;

                pthread_cond_broadcast(&shm->cond);
                sleep(&shm->mutex, delay);

                shm->status = Status::CLOSED;
                pthread_cond_broadcast(&shm->cond);
            }
        }
        
        previous = Car{
            shm->current_floor,
            shm->destination_floor,
            shm->status,
            shm->individual_service_mode,
            shm->close_button,
            shm->open_button
        };
        
        pthread_mutex_unlock(&shm->mutex);  
    }

    return nullptr;
}

enum class Direction { UP, DOWN, NONE };

Direction getDirection(int32_t current, int32_t destination) {
    if (current < destination) {
        return Direction::UP;
    }

    if (current > destination) {
        return Direction::DOWN;
    }

    return Direction::NONE;
}


void monitor_individual_mode (car_shared_mem * shm, int delay) {
    WaitResult result = delay_ms(
    &shm->cond,
    &shm->mutex,
    delay,
    [&] {
            return shm->individual_service_mode;
        }
    );

    switch (result) {
        case WaitResult::Changed:
            // Handle updated state.
            // std::cout << "Oh close button pressed!!" << std::endl;
            // if (shm->close_button) {
            //     shm->close_button = 0;
            //     shm->status = Status::CLOSING;
            // }

            break;

        case WaitResult::TimedOut:
            // Complete the delayed transition.
            // std::cout << "Timed out, this is okay" << std::endl;
            break;

        case WaitResult::Error:
            // Handle failure.
            std::cout << "Error occurred during delay_ms" << std::endl;
            break;
    }
}

static void elevator_state_machine(car_shared_mem * shm, int delay) {
    
    auto previous_state = shm->status;

    while(true) {
        // std::cout << "executing" << std::endl;
        switch (shm->status) {
            
            case Status::BETWEEN:
                sleep(&shm->mutex, delay);

                if (shm->individual_service_mode) {
                    return;
                }

                // std::cout << "a" << std::endl;

                switch (getDirection(shm->current_floor, shm->destination_floor)) {

                    case Direction::UP:
                        if (shm->current_floor == -1) {
                        // Skip 0 because floor progression is form B99..B1 to 1..99 there is no 0th floor
                            shm->current_floor = 1;
                        } else {
                            shm->current_floor++;
                        }
                        break;
                    case Direction::DOWN:
                        if (shm->current_floor == 1) {
                        // Skip 0 because floor progression is form B99..B1 to 1..99 there is no 0th floor
                            shm->current_floor = -1;
                        } else {
                            shm->current_floor--;
                        }
                        break;
                    default: 
                        break;
                }

                // if (shm->current_floor == -1) {
                //     // Skip 0 because floor progression is form B99..B1 to 1..99 there is no 0th floor
                //     shm->current_floor = 1;
                // } else {
                //     shm->current_floor++;
                // }

                shm->status = Status::CLOSED;
                previous_state = Status::BETWEEN;
                pthread_cond_broadcast(&shm->cond);

                break;

            case Status::OPEN:
            {
                WaitResult result = delay_ms(
                    &shm->cond,
                    &shm->mutex,
                    delay,
                    [&] {
                        return shm->close_button;
                    }
                );

                switch (result) {
                    case WaitResult::Changed:
                        if (shm->individual_service_mode) {
                            return;
                        }
                        break;

                    case WaitResult::TimedOut:
                        break;

                    case WaitResult::Error:
                        std::cout << "Error occurred during delay_ms" << std::endl;
                        break;
                }

                if (shm->open_button) {
                    shm->open_button = 0;
                    pthread_cond_broadcast(&shm->cond);
                    sleep(&shm->mutex, delay);

                    if (shm->individual_service_mode) {
                        return;
                    }
                    // monitor_individual_mode()
                } 
                else if (shm->close_button) {
                    // std::cout << "Closing button pressed" << std::endl;
                    shm->close_button = 0;
                    shm->status = Status::CLOSING;
                    pthread_cond_broadcast(&shm->cond);

                    break;
                }

                shm->status = Status::CLOSING;
                previous_state = Status::OPEN;
                pthread_cond_broadcast(&shm->cond);
                break;
            }
            case Status::OPENING:
            {
                sleep(&shm->mutex, delay);

                if (shm->individual_service_mode) {
                    return;
                }

                // opening_cuz = false;
                shm->status = Status::OPEN;
                previous_state = Status::OPENING;
                pthread_cond_broadcast(&shm->cond);

                break;
            }
            case Status::CLOSING:
            {
                sleep(&shm->mutex, delay);

                if (shm->individual_service_mode) {
                    return;
                }

                if (shm->open_button) {
                    shm->open_button = 0;
                    shm->status = Status::OPENING;
                } else {
                    shm->status = Status::CLOSED;
                }

                previous_state = Status::CLOSING;
                pthread_cond_broadcast(&shm->cond);

                break;
            }
            case Status::CLOSED:
                if (previous_state == Status::CLOSING) {
                    sleep(&shm->mutex, delay);

                    if (shm->individual_service_mode) {
                        return;
                    }
                }

                if (previous_state == Status::CLOSING && shm->current_floor == shm->destination_floor) {
                    return;
                }

                if (shm->current_floor != shm->destination_floor) {
                    shm->status = Status::BETWEEN;
                    previous_state = Status::CLOSED;
                    
                    pthread_cond_broadcast(&shm->cond);
                } else {
                    if (shm->open_button) {
                        shm->open_button = 0;
                    }

                    shm->status = Status::OPENING;  
                    previous_state = Status::CLOSED;
                    pthread_cond_broadcast(&shm->cond);
                }
                break;
            default:
                return;
        }
    }
}


static void * handle_normal_operation(void * arg) {    
    context_t * context = reinterpret_cast<context_t *>(arg);
    
    car_shared_mem * shm = context->shared_mem;

    pthread_mutex_lock(&shm->mutex);  
    Car previous{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
    pthread_mutex_unlock(&shm->mutex);  

    while(true) {
        pthread_mutex_lock(&shm->mutex);  
    
        Car changed{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        // Wait for change in destination floor and status 
        while (changed == previous) {
            pthread_cond_wait(&shm->cond, &shm->mutex);
            changed = Car{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        }

        if (previous.destination_floor != changed.destination_floor 
            || previous.status != changed.status 
            || previous.current_floor != changed.current_floor || previous.close_button != changed.close_button || 
            previous.open_button != changed.open_button) {

            if (shm->individual_service_mode == 0) {
                elevator_state_machine(shm, context->delay);
            }
        }

        previous = Car{
            shm->current_floor,
            shm->destination_floor,
            shm->status,
            shm->individual_service_mode,
            shm->close_button,
            shm->open_button
        };
    
        pthread_mutex_unlock(&shm->mutex); 
    }   
}


static void * handle_receive(void * arg)
{

    std::cout << "Receive thread started" << std::endl;
    context_t * context = reinterpret_cast<context_t *>(arg);

    // std::cout << "fd: " << context->sockfd << std::endl;
    const int sockfd = context->sockfd;
    
    car_shared_mem * shm = context->shared_mem;

    std::string message; 

    while(true) {
        int ret = receive_message(sockfd, message);
        // std::cout << "ret " << ret << std::endl;

        if (ret != 0) {
            // we have to detect that car has disconnected to the server, meaning fd will be invalid 
            // we only want to retry if non-error code happened

            if (ret == -1) {
                break;
            }

            continue;
        }

        std::cout << "handle_receive(): " << message << std::endl;

        // two types of messages begin sent from the controller
        if (! message.starts_with("FLOOR")) {
            continue;
        }

        std::string floor = message.substr(6);
        int32_t floor_int = parse_floor(floor);
        
        pthread_mutex_lock(&shm->mutex);
      
        if (shm->individual_service_mode) {
            std::cout << "exiting recieve thread due to indiviudal service mode" << std::endl;
            pthread_mutex_unlock(&shm->mutex);
            break;
        }

        if (shm->status == Status::BETWEEN) {
            while (shm->status == Status::BETWEEN) {
                pthread_cond_wait(&shm->cond, &shm->mutex);
            }

            shm->destination_floor = floor_int;

            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            continue;
        }

        if (shm->current_floor == floor_int) {
            shm->status = Status::OPENING;
            // opening_cuz = true;
        } else {
            shm->destination_floor = floor_int;
        }
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);

        // Car changed{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        // Wait for change in destination floor and status 
        // we only want to change the destination floor if the elevator is not moving (between)
        // while (previous == changed) {
        //     pthread_cond_wait(&shm->cond, &shm->mutex);
        //     changed = Car{shm->current_floor, shm->destination_floor, shm->status, shm->individual_service_mode, shm->close_button, shm->open_button};
        // }


        // if (shm->status != Status::BETWEEN) {
        //     if (shm->current_floor == floor_int) {
        //         shm->status = Status::OPENING;
        //     } else {
        //         shm->destination_floor = floor_int;
        //     }
        //     pthread_cond_broadcast(&shm->cond);
        // }
    }

    return nullptr;
}
