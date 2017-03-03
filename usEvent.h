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

typedef void (*eventDiskCall)(int);
typedef void (*eventPhoneCall)(int, char *,);

struct usEventArg{
	eventDiskCall phoneCall;
	eventPhoneCall eventPhoneCall;
};


#ifdef __cplusplus
}
#endif

#endif

