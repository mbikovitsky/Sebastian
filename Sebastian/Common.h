#pragma once

#include <ntifs.h>


/**
 * Closes an object using a destructor function,
 * then resets it to NULL.
 */
#define CLOSE(object, pfnDestructor) CLOSE_TO_VALUE((object), NULL, (pfnDestructor))

/**
 * Closes an object using a destructor function,
 * then resets the object to the given invalid value.
 */
#define CLOSE_TO_VALUE(object, value, pfnDestructor)                                               \
    CLOSE_TO_VALUE_VARIADIC((object), (value), (pfnDestructor))

/**
 * Closes an object using a destructor function,
 * then resets the object to the given invalid value.
 * Optionally passes additional arguments to the destructor.
 */
#define CLOSE_TO_VALUE_VARIADIC(object, value, pfnDestructor, ...)                                 \
    do                                                                                             \
    {                                                                                              \
        if ((value) != (object))                                                                   \
        {                                                                                          \
            (VOID)((pfnDestructor)((object), __VA_ARGS__));                                        \
            (object) = (value);                                                                    \
        }                                                                                          \
    } while (0)

/**
 * Dereferences a kernel object.
 */
#define DEREFERENCE_OBJECT(pvObject) CLOSE((pvObject), ObfDereferenceObject)

/**
 * Completes an IRP and resets the pointer to NULL.
 */
#define COMPLETE_IRP(ptIrp, ePriorityBoost)                                                        \
    CLOSE_TO_VALUE_VARIADIC((ptIrp), NULL, IofCompleteRequest, (ePriorityBoost))
