/**
 * PCIe and RTL8125 Network Test Application
 * 
 * This file provides a simple test application to demonstrate
 * PCIe ATU configuration and RTL8125 network controller usage.
 */

#include "lib/t_logger.h"
#include "dev/pcie_test.h"

/**
 * main - Entry point for PCIe test
 * 
 * Call this function from your main entry point (t_entry.c)
 * to run the PCIe and network tests.
 */
void
pcie_network_test_main(void)
{
    logger_info("*************************************************\n");
    logger_info("*                                               *\n");
    logger_info("*  PCIe ATU and RTL8125 Network Test Suite     *\n");
    logger_info("*                                               *\n");
    logger_info("*************************************************\n");
    logger_info("\n");

    logger_info("This test will:\n");
    logger_info("  1. Configure PCIe Address Translation Unit (ATU)\n");
    logger_info("  2. Enumerate PCIe bus and detect devices\n");
    logger_info("  3. Initialize RTL8125 network controller\n");
    logger_info("  4. Send ICMP ping request\n");
    logger_info("  5. Receive and process ping reply\n");
    logger_info("\n");

    logger_info("Starting test sequence...\n");
    logger_info("\n");

    /* Run the main PCIe ATU test */
    test_dw_pcie_atu();
   

    logger_info("\n");
    logger_info("*************************************************\n");
    logger_info("*           Test Sequence Complete              *\n");
    logger_info("*************************************************\n");
    logger_info("\n");
}
