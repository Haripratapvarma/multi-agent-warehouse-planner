using System.Windows.Threading;
using Warehouse.ViewModels.Services;

namespace Warehouse.Desktop.Services;

/// <summary>Playback timer that ticks on the WPF UI thread.</summary>
public sealed class DispatcherPlaybackTimer : IPlaybackTimer
{
    private readonly DispatcherTimer _timer = new();

    public DispatcherPlaybackTimer()
    {
        _timer.Tick += (_, e) => Tick?.Invoke(this, e);
    }

    public event EventHandler? Tick;

    public TimeSpan Interval
    {
        get => _timer.Interval;
        set => _timer.Interval = value;
    }

    public bool IsRunning => _timer.IsEnabled;

    public void Start() => _timer.Start();

    public void Stop() => _timer.Stop();
}
