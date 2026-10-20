using Microsoft.Win32.SafeHandles;

namespace Warehouse.Interop.Native;

// SafeHandles make sure native memory is always released: when Dispose is
// called, or by the finalizer if someone forgets. They also stop the handle
// being freed while a native call is still using it.

/// <summary>Owns a native WhScenario*.</summary>
internal sealed class ScenarioSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    // The marshaller creates instances through this constructor.
    public ScenarioSafeHandle() : base(ownsHandle: true)
    {
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.wh_scenario_free(handle);
        return true;
    }
}

/// <summary>Owns a native WhPlan*.</summary>
internal sealed class PlanSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public PlanSafeHandle() : base(ownsHandle: true)
    {
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.wh_plan_free(handle);
        return true;
    }
}
