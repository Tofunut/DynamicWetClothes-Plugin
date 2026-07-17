#include "Utility/DWCError.h"

#include "Containers/UnrealString.h"

namespace DWC::Error
{
    void SetMessage(FString* OutErrorMessage, const TCHAR* Message)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = Message;
        }
    }

    void SetMessage(FString* OutErrorMessage, const FString& Message)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = Message;
        }
    }
}
