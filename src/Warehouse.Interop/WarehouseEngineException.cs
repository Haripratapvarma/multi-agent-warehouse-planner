namespace Warehouse.Interop;

/// <summary>Status codes returned by the native engine (WhStatus in c_api.h).</summary>
public enum EngineStatus
{
    Ok = 0,
    InvalidArgument = 1,
    OutOfRange = 2,
    BufferTooSmall = 3,
    IoError = 4,
    FormatError = 5,
    InternalError = 6,
}

/// <summary>Thrown when a native engine call returns an error code.</summary>
public sealed class WarehouseEngineException : Exception
{
    public WarehouseEngineException(EngineStatus status, string message)
        : base(message)
    {
        Status = status;
    }

    public EngineStatus Status { get; }
}
