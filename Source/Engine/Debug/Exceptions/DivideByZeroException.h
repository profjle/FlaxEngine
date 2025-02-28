// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#include "Engine/Debug/Exception.h"

namespace Log
{
    /// <summary>
    /// The exception that is thrown when an number is divided by zero
    /// </summary>
    class DivideByZeroException : public Exception
    {
    public:

        /// <summary>
        /// Init
        /// </summary>
        DivideByZeroException()
            : DivideByZeroException(String::Empty)
        {
        }

        /// <summary>
        /// Creates default exception with additional data
        /// </summary>
        /// <param name="message">Additional information that help describe error</param>
        DivideByZeroException(const String& additionalInfo)
            : Exception(TEXT("Tried to divide value by zero"), additionalInfo)
        {
        }
    };
}
