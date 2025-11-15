#ifndef __PCIE_TEST_H__
#define __PCIE_TEST_H__

/**
 * test_dw_pcie_atu - Test DesignWare PCIe ATU configuration and RTL8125 driver
 * 
 * This function demonstrates:
 * 1. PCIe ATU (Address Translation Unit) configuration
 * 2. PCIe bus enumeration and device detection
 * 3. RTL8125 network controller initialization
 * 4. Basic networking functionality (ICMP ping)
 * 
 * The function will:
 * - Configure the PCIe ATU for configuration space access
 * - Scan the PCIe bus for connected devices
 * - Detect and identify RTL8125/RTL8169 network controllers
 * - Initialize the network controller
 * - Send an ICMP echo request (ping)
 * - Wait for and process the echo reply
 * 
 * All operations are logged with detailed information for debugging.
 */
void
test_dw_pcie_atu(void);

/**
 * pcie_network_test_main - Main entry point for PCIe network testing
 * 
 * This is a convenience wrapper function that calls test_dw_pcie_atu().
 * It's provided as a simplified entry point for the PCIe network test suite.
 */
void
pcie_network_test_main(void);

#endif  // __PCIE_TEST_H__#endif /* __PCIE_TEST_H__ */
