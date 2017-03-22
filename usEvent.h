/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#ifndef __USEVENT_H_
#define __USEVENT_H_

#ifdef __cplusplus
	 extern "C" {
#endif

typedef void (*eventPhoneCall)(int);
typedef void (*eventDiskCall)(int, char *);

struct usEventArg{
	eventDiskCall eventDiskCall;
	eventPhoneCall eventPhoneCall;
};

int32_t usEvent_init(struct usEventArg *evarg);

#ifdef __cplusplus
}
#endif

#endif

