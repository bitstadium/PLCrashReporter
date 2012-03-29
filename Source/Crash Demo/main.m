/*
 * Author: Landon Fuller <landonf@plausiblelabs.com>
 *
 * Copyright (c) 2008-2009 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#import <Foundation/Foundation.h>
#import "CrashReporter.h"
#import <pthread.h>

@interface TestObject : NSObject

- (void)aMethod;

@end

@implementation TestObject

- (void)aMethod
{
    /* Trigger a crash in Obj-C */
    [(NSObject *)(((uintptr_t *)NULL)[1]) self];
}

@end

/* A custom post-crash callback */
void post_crash_callback (siginfo_t *info, ucontext_t *uap, void *context) {
    // this is not async-safe, but this is a test implementation
//    NSLog(@"post crash callback: signo=%d, uap=%p, context=%p", info->si_signo, uap, context);
}

void thread_stackFrame3 (int crashThreaded) {
    /* Trigger a threaded crash */
    if (crashThreaded == 1)
    	//printf("%s", (char *)0x123124);
	    ((char *)NULL)[2] = 0;
    else if (crashThreaded == 3) {
        TestObject *obj = [[TestObject alloc] init];
        [obj aMethod];
    }
    sleep(2);
}

void thread_stackFrame2 (int crashThreaded) {
	thread_stackFrame3(crashThreaded);
}

void *thread_stackFrame (void *arg) {
	thread_stackFrame2((int)(intptr_t)arg);
    return NULL;
}

void stackFrame2 (int crashThreaded) {
    pthread_t thread;
    
    pthread_create(&thread, NULL, thread_stackFrame, (void *)(intptr_t)crashThreaded);
    
    /* Trigger a crash */
    if (crashThreaded == 0)
		((char *)NULL)[1] = 0;
    else if (crashThreaded == 2) {
        TestObject *obj = [[TestObject alloc] init];
        [obj aMethod];
    }
    
    sleep(2);
}

void stackFrame (int crashThreaded) {
	stackFrame2(crashThreaded);
}

/* If a crash report exists, make it accessible via iTunes document sharing. This is a no-op on Mac OS X. */
static void save_crash_report () {
    if (![[PLCrashReporter sharedReporter] hasPendingCrashReport]) 
        return;

#if TARGET_OS_IPHONE
    NSFileManager *fm = [NSFileManager defaultManager];
    NSError *error;
    
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [paths objectAtIndex:0];
    if (![fm createDirectoryAtPath: documentsDirectory withIntermediateDirectories: YES attributes:nil error: &error]) {
        NSLog(@"Could not create documents directory: %@", error);
        return;
    }


    NSData *data = [[PLCrashReporter sharedReporter] loadPendingCrashReportDataAndReturnError: &error];
    if (data == nil) {
        NSLog(@"Failed to load crash report data: %@", error);
        return;
    }

    NSString *outputPath = [documentsDirectory stringByAppendingPathComponent: @"demo.plcrash"];
    if (![data writeToFile: outputPath atomically: YES]) {
        NSLog(@"Failed to write crash report");
    }
    
    NSLog(@"Saved crash report to: %@", outputPath);
#endif
}

/* argv[1]:
    0: Crash on main thread.
    1: Crash on secondary thread.
    2: Crash Objective-C on main thread.
    3: Crash Objective-C on seconary thread.
    *: Don't crash.
*/
int main (int argc, char *argv[]) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSError *error = nil;

    /* Save any existing crash report. */
    save_crash_report();
    
    /* Set up post-crash callbacks */
    PLCrashReporterCallbacks cb = {
        .version = 0,
        .context = (void *) 0xABABABAB,
        .handleSignal = post_crash_callback
    };
    [[PLCrashReporter sharedReporter] setCrashCallbacks: &cb];

    /* Enable the crash reporter */
    if (![[PLCrashReporter sharedReporter] enableCrashReporterAndReturnError: &error]) {
        NSLog(@"Could not enable crash reporter: %@", error);
    }

    /* Add another stack frame */
    stackFrame(argc > 1 ? atoi(argv[1]) : 0);

    [pool release];
}
