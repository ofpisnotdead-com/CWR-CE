#pragma once

namespace Poseidon
{
enum class DownloadFileResult
{
    Success,
    TransientFailure,
    PermanentFailure,
    Cancelled
};
}
