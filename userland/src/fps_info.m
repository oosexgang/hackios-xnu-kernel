/*
 * fps_info: what CoreAnimation and UIKit believe about the screen's frame rate, from inside a process of the system:
 * CADisplay's current, preferred and available modes with their refresh rates, its own idea of the most frames a
 * second and of the shortest frame, the same of UIScreen, and what MobileGestalt says of the device's display. The
 * panel runs at 120 Hz here (fb_measure=1) while the frames came at 60 (tools/frames.py): this says which of them
 * holds the rate at 60. Every property is asked only if the object answers it; nothing is set.
 *
 *   usage: fps_info          (RC_JOBS: a line /redwood/bin/fps_info, after SpringBoard is up)
 */
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>

static void
show(const char *what, id object, const char *const *selectors)
{
	printf("fps_info: %s: %s\n", what, object != nil ? [[object description] UTF8String] : "(none)");
	if (object == nil) {
		return;
	}
	for (unsigned i = 0; selectors[i] != NULL; i++) {
		SEL selector = sel_registerName(selectors[i]);
		Method method = class_getInstanceMethod(object_getClass(object), selector);
		char type[64] = "";

		if (method == NULL) {
			continue;
		}
		method_getReturnType(method, type, sizeof(type));
		switch (type[0]) {
		case 'd':
			printf("fps_info:   %s = %g\n", selectors[i], ((double (*)(id, SEL))objc_msgSend)(object, selector));
			break;
		case 'f':
			printf("fps_info:   %s = %g\n", selectors[i], ((float (*)(id, SEL))objc_msgSend)(object, selector));
			break;
		case 'q': case 'Q': case 'l': case 'L': case 'i': case 'I': case 'B': case 'c':
			printf("fps_info:   %s = %lld\n", selectors[i], (long long)((long (*)(id, SEL))objc_msgSend)(object, selector));
			break;
		case '@':
			printf("fps_info:   %s = %s\n", selectors[i],
			    [[((id (*)(id, SEL))objc_msgSend)(object, selector) description] UTF8String]);
			break;
		default:
			printf("fps_info:   %s: of type %s, not shown\n", selectors[i], type);
		}
	}
}

int
main(void)
{
	@autoreleasepool {
		static const char *const display[] = { "maximumFramesPerSecond", "minimumFrameDuration", "refreshRate",
			"heartbeatRate", "currentMode", "preferredMode", "availableModes", "frameRateLimit", "isFrameRateLimited",
			"supportsExtendedColors", "name", NULL };
		static const char *const mode[] = { "refreshRate", "width", "height", "pixelWidth", "pixelHeight", NULL };
		static const char *const screen[] = { "maximumFramesPerSecond", "_refreshRate", "_maximumFramesPerSecond", NULL };
		Class cadisplay = objc_getClass("CADisplay");
		id main = cadisplay != Nil ? ((id (*)(id, SEL))objc_msgSend)((id)cadisplay, sel_registerName("mainDisplay")) : nil;

		show("CADisplay mainDisplay", main, display);
		if (main != nil) {
			id current = ((id (*)(id, SEL))objc_msgSend)(main, sel_registerName("currentMode"));

			show("its current mode", current, mode);
			for (id each in ((id (*)(id, SEL))objc_msgSend)(main, sel_registerName("availableModes"))) {
				show("an available mode", each, mode);
			}
		}
		if (dlopen("/System/Library/Frameworks/UIKit.framework/UIKit", RTLD_NOW) != NULL) {
			Class uiscreen = objc_getClass("UIScreen");
			id screen_main = uiscreen != Nil ? ((id (*)(id, SEL))objc_msgSend)((id)uiscreen, sel_registerName("mainScreen")) : nil;

			show("UIScreen mainScreen", screen_main, screen);
		}
		void *gestalt = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_NOW);
		CFTypeRef (*answer)(CFStringRef) = gestalt != NULL ? (CFTypeRef (*)(CFStringRef))dlsym(gestalt, "MGCopyAnswer") : NULL;
		static const char *const keys[] = { "DeviceSupportsProMotion", "DeviceSupports120HzRefreshRate", "main-screen-class",
			"DeviceSupportsDynamicRefreshRate", "MaxRefreshRate", "main-screen-refresh-rate", NULL };

		for (unsigned i = 0; answer != NULL && keys[i] != NULL; i++) {
			CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
			CFTypeRef value = answer(key);

			printf("fps_info: MobileGestalt %s = %s\n", keys[i], value != NULL ? [[(__bridge id)value description] UTF8String] : "(none)");
			if (value != NULL) {
				CFRelease(value);
			}
			CFRelease(key);
		}
	}
	return 0;
}
