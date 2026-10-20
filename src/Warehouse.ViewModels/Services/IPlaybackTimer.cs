namespace Warehouse.ViewModels.Services;

/// <summary>
/// A repeating timer that raises <see cref="Tick"/> on the UI thread.
/// The desktop app wraps a DispatcherTimer; tests raise ticks by hand.
/// </summary>
public interface IPlaybackTimer
{
    TimeSpan Interval { get; set; }

    bool IsRunning { get; }

    event EventHandler? Tick;

    void Start();

    void Stop();
}
