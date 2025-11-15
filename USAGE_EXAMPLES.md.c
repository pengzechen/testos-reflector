/**
 * Example: Integration into t_entry.c
 * 
 * This file shows how to integrate the PCIe and RTL8125 test
 * into your main entry point.
 */

/* Include the test header */
#include "include/dev/pcie_test.h"
#include "include/lib/t_logger.h"

/* Example 1: Direct call from main */
void
example_direct_call(void)
{
    logger_info("=== Example 1: Direct PCIe Test Call ===\n");

    /* Call the test function directly */
    test_dw_pcie_atu();
}

/* Example 2: Conditional execution based on command */
void
example_conditional(int test_id)
{
    switch (test_id) {
        case 1:
            logger_info("Running PCIe ATU test...\n");
            test_dw_pcie_atu();
            break;
        case 2:
            logger_info("Running other test...\n");
            /* Other tests */
            break;
        default:
            logger_warn("Unknown test ID: %d\n", test_id);
            break;
    }
}

/* Example 3: Using the wrapper function */
extern void
pcie_network_test_main(void);

void
example_wrapper(void)
{
    logger_info("=== Example 3: Using Wrapper Function ===\n");
    pcie_network_test_main();
}

/*
 * Example 4: Integration in your actual t_entry.c
 * 
 * Add this to src/t_entry.c:
 */
#if 0 /* Example code - not compiled */

    #include "include/dev/pcie_test.h"

void t_main(void) {
	/* Your existing initialization code */
	logger_info("TestOS-Reflector Starting...\n");
	
	/* Initialize hardware */
	// uart_init();
	// timer_init();
	// gic_init();
	// ...
	
	/* Run PCIe and Network test */
	logger_info("Starting PCIe and Network tests...\n");
	test_dw_pcie_atu();
	
	/* Continue with other operations */
	// ...
	
	/* Main loop */
	while (1) {
		// Your main loop code
	}
}

#endif /* Example code */

/* Example 5: Advanced - Customized configuration */
void
example_custom_config(void)
{
    logger_info("=== Example 5: Custom Configuration ===\n");

    /* You can modify the addresses before calling the test */
    /* Note: The actual implementation would need to support
	 * parameter passing for this to work */

    logger_info("Configuring custom IP addresses...\n");
    // uint8_t my_ip[4] = {192, 168, 1, 100};
    // uint8_t target_ip[4] = {192, 168, 1, 1};

    logger_info("Running test with custom configuration...\n");
    test_dw_pcie_atu();
}

/* Example 6: Error handling */
void
example_with_error_handling(void)
{
    logger_info("=== Example 6: With Error Handling ===\n");

    /* Note: Current implementation doesn't return error codes
	 * Future enhancement could add return values */

    logger_info("Running PCIe test...\n");
    test_dw_pcie_atu();
    logger_info("Test completed (check logs for results)\n");

    /* Check system state after test */
    // if (pcie_link_up()) {
    //     logger_info("PCIe link is up\n");
    // } else {
    //     logger_error("PCIe link is down\n");
    // }
}

/* Example 7: Multiple iterations for stress testing */
void
example_stress_test(void)
{
    logger_info("=== Example 7: Stress Test ===\n");

    const int iterations = 10;

    for (int i = 0; i < iterations; i++) {
        logger_info("\n--- Iteration %d/%d ---\n", i + 1, iterations);
        test_dw_pcie_atu();

        /* Add delay between iterations */
        // mdelay(1000);  /* 1 second delay */
    }

    logger_info("\nStress test complete: %d iterations\n", iterations);
}

/**
 * Main function examples - choose one based on your needs
 */

/* Simple version - just run the test once at startup */
void
simple_main(void)
{
    test_dw_pcie_atu();
}

/* Interactive version - run based on user command */
void
interactive_main(void)
{
    char cmd;

    while (1) {
        logger_info("\nCommands:\n");
        logger_info("  p - Run PCIe test\n");
        logger_info("  q - Quit\n");
        logger_info("Enter command: ");

        /* Read command (implementation depends on your UART driver) */
        // cmd = uart_getchar();

        // switch (cmd) {
        // case 'p':
        // 	test_dw_pcie_atu();
        // 	break;
        // case 'q':
        // 	return;
        // default:
        // 	logger_warn("Unknown command\n");
        // }
    }
}

/* Menu-driven version */
void
menu_main(void)
{
    logger_info("\n");
    logger_info("========================================\n");
    logger_info("   TestOS-Reflector Test Menu\n");
    logger_info("========================================\n");
    logger_info("\n");
    logger_info("Available tests:\n");
    logger_info("  1. PCIe ATU and RTL8125 Network Test\n");
    logger_info("  2. NPU Test\n");
    logger_info("  3. Memory Test\n");
    logger_info("  4. Exit\n");
    logger_info("\n");

    int choice = 1; /* Default choice */

    switch (choice) {
        case 1:
            logger_info("Running PCIe and Network test...\n");
            test_dw_pcie_atu();
            break;
        case 2:
            logger_info("Running NPU test...\n");
            /* Call NPU test function */
            break;
        case 3:
            logger_info("Running Memory test...\n");
            /* Call memory test function */
            break;
        case 4:
            logger_info("Exiting...\n");
            return;
        default:
            logger_warn("Invalid choice\n");
    }
}
