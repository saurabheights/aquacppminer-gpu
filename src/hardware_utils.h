/**
 * @brief Checks hardware for opencl platforms and devices.
 *
 * @return int
 */

bool checkPlatforms();

/**
 * @brief Checks available gpu devices on all platforms.
 *
 * @param silent To print each device information
 * @return int Number of gpus found.
 */
int checkGpuDevices(bool silent = false);

/**
 * @brief Get the Gpu Devices objects from all available platforms.
 *
 * @param result The vector is populated with devices. Memor Management has to be taken care by caller. ToDo - See smart pointers.
 */
void getGpuDevices(std::vector<cl_device_id*>& result);