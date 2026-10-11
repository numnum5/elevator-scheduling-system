#include <iostream>
#include <string>
#include <format>
#include <stdint.h>

enum class Status {
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
//   char current_floor[4];           // C string in the range B99-B1 and 1-999
//   char destination_floor[4];       // Same format as above
//   char status[8];                  // C string indicating the elevator's status
  uint8_t open_button;             // 1 if open doors button is pressed, else 0
  uint8_t close_button;            // 1 if close doors button is pressed, else 0
  uint8_t door_obstruction;        // 1 if obstruction detected, else 0
  uint8_t overload;                // 1 if overload detected
  uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
  uint8_t individual_service_mode; // 1 if in individual service mode, else 0
  uint8_t emergency_mode;          // 1 if in emergency mode, else 0
};



// Internal control
int main(int argc, char * argv[]){
    
    if(argc != 3){
        std::cerr << "" << std::endl;
        return 1;
    }

    // Store car name and operation
    std::string car_name = argv[1];
    std::string operation = argv[2];
    
    // Format the sharedmemory name
    // char * shared_mem_name = concat_strings(2, "/car", car_name);

    std::string shared_mem_name = std::format("/car {}", car_name);

    // Get shared object from shared memory region
    car_shared_mem * shared_memory = new car_shared_mem;


    // Acquire mutex reference
    pthread_mutex_t * mutext_ptr = &(shared_memory->mutex);

    // Critical section
    pthread_mutex_lock(&shared_memory->mutex);

    if (operation == "open") {
        shared_memory->open_button = 1;
    }else if(operation ==  "close") {
        shared_memory->close_button = 1;
    }else if(operation == "stop") {
        shared_memory->emergency_stop = 1;
    }else if(operation ==  "service_on") {
        shared_memory->individual_service_mode = 1;
        shared_memory->emergency_mode = 0;
    }else if(operation == "service_off") {
        shared_memory->individual_service_mode = 0;
    }else if(operation == "up" || operation == "down") {
        if(shared_memory->individual_service_mode) {
            switch (shared_memory->status) {
                case Status::OPEN:
                case Status::OPENING:
                case Status::CLOSING:
                case Status::BETWEEN:
                    pthread_mutex_unlock(&shared_memory->mutex);
                    std::cerr << "Operation not allowed" << std::endl;
                    return 1;
                case Status::CLOSED:
                    if (operation == "up") {
                        int32_t nextFloor = (shared_memory->current_floor + 1);
                        shared_memory->destination_floor = nextFloor == 0 ? 1 : nextFloor;
                        break;
                    } else if (operation == "down") {
                        int32_t nextFloor = (shared_memory->current_floor - 1);
                        shared_memory->destination_floor = nextFloor == 0 ? -1 : nextFloor;
                        break;
                    } else {
                        std::cerr << "Unexpected internal operation\n";
                        return 1;
                    }
                    break;
                default:
                    pthread_mutex_unlock(&shared_memory->mutex);
                    std::cerr << "DA" << std::endl;
                    return 1;
            }
        } else {
            pthread_mutex_unlock(&shared_memory->mutex);
            std::cerr << "DA" << std::endl;
            return 1;
        }
    } else {
        pthread_mutex_unlock(&shared_memory->mutex);
        std::cerr << "DA" << std::endl;
        return 1;
    }
    
    // Broadcast any changes
    pthread_cond_broadcast(&(shared_memory->cond)); 
    pthread_mutex_unlock(&shared_memory->mutex);

    return 0;
}
