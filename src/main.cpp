#include <iostream>
#include <stdint.h>


enum class Status
{
    OPENING,
    OPEN,
    CLOSING,
    CLOSED,
    BETWEEN
};

typedef struct {
//   pthread_mutex_t mutex;           // Locked while accessing struct contents
//   pthread_cond_t cond;             // Signalled when the contents change
  std::string current_floor;           // C string in the range B99-B1 and 1-999
  std::string destination_floor;       // Same format as above
  Status status;                  // C string indicating the elevator's status
  uint8_t open_button;             // 1 if open doors button is pressed, else 0
  uint8_t close_button;            // 1 if close doors button is pressed, else 0
  uint8_t door_obstruction;        // 1 if obstruction detected, else 0
  uint8_t overload;                // 1 if overload detected
  uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
  uint8_t individual_service_mode; // 1 if in individual service mode, else 0
  uint8_t emergency_mode;          // 1 if in emergency mode, else 0
} car_shared_mem;

int main(int argc, char* argv[]) {
    if (argc != 2) return 1;

    const std::string filename = argv[1];



    std::cout << filename << std::endl;

}