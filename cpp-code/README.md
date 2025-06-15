# README for MyProject

## Overview
Dumps JSON data to csv table for further processing

## Project Structure
```
cpp-code
├── src               # Source files
├── include           # Header files
├── main.cc           # Fain file 
├── CMakeLists.txt   # CMake configuration file
├── conanfile.txt    # Conan dependency management file
└── README.md        # Project documentation
```

## Setup Instructions

1. **Clone the repository:**
   ```bash
   git clone https://github.com/0xfedcafe/alcotrade.git 
   or 
   git clone git@github.com:0xfedcafe/alcotrade.git
   cd alcotrade/cpp-code
   ```

2. **Install Conan:**
   Make sure you have Conan installed. If not, you can install it using pip:
   ```bash
   pip install conan
   ```

3. **Install dependencies:**
   Run the following command to install the required dependencies specified in `conanfile.txt`:
   ```bash
   conan install . --build=missing --settings=build_type=Debug
   ```

4. **Build the project:**
   Use CMake to configure and build the project:
   ```bash
   cmake --preset conan-debug -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE
   cd build/Debug
   make all
   ```

## Usage
After building the project, you can run the executable generated in the `build/Debug` directory.
./alcotrade