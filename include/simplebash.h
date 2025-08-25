#ifndef __SIMPLEBASH_H__
#define __SIMPLEBASH_H__

/*
 * Simple Bash - A basic shell implementation for TestOS
 * 
 * This header defines the interface for the simple shell
 * that provides basic command-line functionality.
 */

// Initialize the shell system
void
simplebash_init(void);

// Main shell loop - call this instead of WFI
// This function checks for input and processes commands
void
simplebash_run(void);

#endif  // __SIMPLEBASH_H__
