/* update_fetch.h on macOS: NSURLSession, without a cache or cookies (an ephemeral session), compiled without ARC. */
#include "update_fetch.h"

#include "update_check.h"

#import <Foundation/Foundation.h>

#include <SDL3/SDL_stdinc.h>

#include <stdio.h>
#include <string.h>

/* What the completion handler leaves for the check's thread, which waits for it. */
typedef struct fetched {
    char *body;
    size_t length;
    int status;
    char error[256];
} fetched;

/* A redirection to https:// alone. */
@interface OraclesUpdateRedirect : NSObject <NSURLSessionTaskDelegate>
@end

@implementation OraclesUpdateRedirect
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response
        newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))completionHandler
{
    (void)session; (void)task; (void)response;
    completionHandler([[request.URL.scheme lowercaseString] isEqualToString:@"https"] ? request : nil);
}
@end

int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity)
{
    (void)cancel;
    fetched *got = SDL_calloc(1, sizeof *got);   /* the handler's, which runs on the session's queue */
    if (!got) { snprintf(error, capacity, "out of memory"); return 0; }
    @autoreleasepool {
        NSString *text = [NSString stringWithUTF8String:url];
        NSURL *address = text ? [NSURL URLWithString:text] : nil;
        if (!address || ![[address.scheme lowercaseString] isEqualToString:@"https"]) {
            snprintf(error, capacity, "not an https:// address");
            SDL_free(got);
            return 0;
        }
        NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = timeout_ms / 1000.0;
        configuration.timeoutIntervalForResource = timeout_ms / 1000.0;
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        configuration.HTTPAdditionalHeaders = @{ @"Accept": [NSString stringWithUTF8String:ORACLES_UPDATE_ACCEPT],
                                                 @"User-Agent": [NSString stringWithUTF8String:ORACLES_UPDATE_USER_AGENT] };
        OraclesUpdateRedirect *redirect = [[OraclesUpdateRedirect alloc] init];
        NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration delegate:redirect delegateQueue:nil];
        [redirect release];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask *task = [session dataTaskWithURL:address completionHandler:^(NSData *data, NSURLResponse *response, NSError *failure) {
            if (failure) snprintf(got->error, sizeof got->error, "%s", failure.localizedDescription.UTF8String ? failure.localizedDescription.UTF8String : "failed");
            else if (![response isKindOfClass:[NSHTTPURLResponse class]]) snprintf(got->error, sizeof got->error, "not an HTTP answer");
            else if (data.length > ORACLES_UPDATE_MAX_BODY) snprintf(got->error, sizeof got->error, "an answer larger than %d bytes", ORACLES_UPDATE_MAX_BODY);
            else if ((got->body = SDL_malloc(data.length + 1)) != NULL) {
                if (data.length) memcpy(got->body, data.bytes, data.length);
                got->body[data.length] = 0;
                got->length = data.length;
                got->status = (int)((NSHTTPURLResponse *)response).statusCode;
            } else snprintf(got->error, sizeof got->error, "out of memory");
            dispatch_semaphore_signal(done);
        }];
        [task resume];
        /* The session's own timeout ends the task, which always calls its handler. */
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        dispatch_release(done);
        [session finishTasksAndInvalidate];
    }
    const int ok = got->body != NULL;
    if (ok) {
        *body = got->body;
        *length = got->length;
        *status = got->status;
    } else snprintf(error, capacity, "%s", got->error);
    SDL_free(got);
    return ok;
}
