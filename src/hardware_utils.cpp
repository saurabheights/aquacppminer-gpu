#include <CL/cl.h>

#include <iostream>
#include <vector>

bool checkPlatforms() {
    // Define attributes to fetch for each platform.
    const char* attributeNames[5] = {"Name", "Vendor", "Version", "Profile", "Extensions"};
    const cl_platform_info attributeTypes[5] = {CL_PLATFORM_NAME, CL_PLATFORM_VENDOR,
                                                CL_PLATFORM_VERSION, CL_PLATFORM_PROFILE, CL_PLATFORM_EXTENSIONS};
    const int attributeCount = sizeof(attributeNames) / sizeof(char*);

    // get platform count and all platforms.
    cl_uint platformCount;
    clGetPlatformIDs(0, NULL, &platformCount);
    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * platformCount);
    clGetPlatformIDs(platformCount, platforms, NULL);

    // For each platform print all attributes
    for (int i = 0; i < platformCount; i++) {
        printf("\n %d. Platform \n", i + 1);
        for (int j = 0; j < attributeCount; j++) {
            // get platform attribute value size and platform attribute value
            size_t infoSize;
            clGetPlatformInfo(platforms[i], attributeTypes[j], 0, NULL, &infoSize);
            char* info = (char*)malloc(infoSize);
            clGetPlatformInfo(platforms[i], attributeTypes[j], infoSize, info, NULL);
            printf("  %d.%d %-11s: %s\n", i + 1, j + 1, attributeNames[j], info);
            free(info);
        }
        printf("\n");
    }

    free(platforms);
    return true;
}

int checkGpuDevices(bool silent = false) {
    if (!silent)
        printf("Checking available gpu devices:\n");
    // get all platforms
    cl_uint platformCount;
    clGetPlatformIDs(0, NULL, &platformCount);
    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * platformCount);
    clGetPlatformIDs(platformCount, platforms, NULL);

    int totalGpuDeviceCount = 0;

    for (int i = 0; i < platformCount; i++) {
        // get all devices
        cl_uint deviceCount;
        // ToDo - Add ALL devices check to make miner work both on CPU and GPU.
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &deviceCount);
        if (deviceCount <= 0)
            continue;
        cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * deviceCount);
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, deviceCount, devices, NULL);

        // for each device print critical attributes
        for (int j = 0; j < deviceCount; j++) {
            // print device name
            size_t valueSize;
            clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 0, NULL, &valueSize);
            char* value = (char*)malloc(valueSize);
            clGetDeviceInfo(devices[j], CL_DEVICE_NAME, valueSize, value, NULL);
            if (!silent)
                printf("%d. Device: %s\n", totalGpuDeviceCount + j + 1, value);
            free(value);

            // print hardware device version
            clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, 0, NULL, &valueSize);
            value = (char*)malloc(valueSize);
            clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, valueSize, value, NULL);
            if (!silent)
                printf(" %d.%d Hardware version: %s\n", totalGpuDeviceCount + j + 1, 1, value);
            free(value);

            // print software driver version
            clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, 0, NULL, &valueSize);
            value = (char*)malloc(valueSize);
            clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, valueSize, value, NULL);
            if (!silent)
                printf(" %d.%d Software version: %s\n", totalGpuDeviceCount + j + 1, 2, value);
            free(value);

            // print c version supported by compiler for device
            clGetDeviceInfo(devices[j], CL_DEVICE_OPENCL_C_VERSION, 0, NULL, &valueSize);
            value = (char*)malloc(valueSize);
            clGetDeviceInfo(devices[j], CL_DEVICE_OPENCL_C_VERSION, valueSize, value, NULL);
            if (!silent)
                printf(" %d.%d OpenCL C version: %s\n", totalGpuDeviceCount + j + 1, 3, value);
            free(value);

            // print parallel compute units
            cl_uint maxComputeUnits;
            clGetDeviceInfo(devices[j], CL_DEVICE_MAX_COMPUTE_UNITS,
                            sizeof(maxComputeUnits), &maxComputeUnits, NULL);
            if (!silent)
                printf(" %d.%d Parallel compute units: %d\n", totalGpuDeviceCount + j + 1, 4, maxComputeUnits);
        }
        totalGpuDeviceCount += deviceCount;

        free(devices);
    }

    free(platforms);
    return totalGpuDeviceCount;
}

void getGpuDevices(std::vector<cl_device_id*>& result) {
    // get all platforms
    cl_uint platformCount;
    clGetPlatformIDs(0, NULL, &platformCount);
    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * platformCount);
    clGetPlatformIDs(platformCount, platforms, NULL);

    int totalGpuDeviceCount = 0;

    for (int i = 0; i < platformCount; i++) {
        // get all devices
        cl_uint deviceCount;
        cl_int status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &deviceCount);
        if (deviceCount <= 0)
            continue;
        cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * deviceCount);
        status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, deviceCount, devices, NULL);
        totalGpuDeviceCount += deviceCount;
        for (size_t deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
            result.push_back(&devices[deviceIndex]);
        }
    }

    free(platforms);
}
