using System.Globalization;
using System.Windows;
using System.Windows.Media;
using Warehouse.Interop;
using Warehouse.ViewModels;

namespace Warehouse.Desktop.Controls;

/// <summary>
/// Draws the warehouse grid, each robot's planned path as a faint line,
/// pickup markers and the robots as numbered coloured circles.
/// It only reads the bound data; all logic lives in the view model.
/// </summary>
public sealed class WarehouseCanvas : FrameworkElement
{
    public static readonly DependencyProperty ScenarioProperty = DependencyProperty.Register(
        nameof(Scenario), typeof(WarehouseScenario), typeof(WarehouseCanvas),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty PlanProperty = DependencyProperty.Register(
        nameof(Plan), typeof(PlanOutcome), typeof(WarehouseCanvas),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty RobotsProperty = DependencyProperty.Register(
        nameof(Robots), typeof(IReadOnlyList<RobotState>), typeof(WarehouseCanvas),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public WarehouseScenario? Scenario
    {
        get => (WarehouseScenario?)GetValue(ScenarioProperty);
        set => SetValue(ScenarioProperty, value);
    }

    public PlanOutcome? Plan
    {
        get => (PlanOutcome?)GetValue(PlanProperty);
        set => SetValue(PlanProperty, value);
    }

    public IReadOnlyList<RobotState>? Robots
    {
        get => (IReadOnlyList<RobotState>?)GetValue(RobotsProperty);
        set => SetValue(RobotsProperty, value);
    }

    // ---- Colours ----

    private static readonly Brush BackgroundBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xF4, 0xF5, 0xF7)));
    private static readonly Brush FreeBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xFF, 0xFF, 0xFF)));
    private static readonly Brush WallBrush = Frozen(new SolidColorBrush(Color.FromRgb(0x37, 0x47, 0x4F)));
    private static readonly Brush ShelfBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xC8, 0xA1, 0x65)));
    private static readonly Brush StationBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xC8, 0xE6, 0xC9)));
    private static readonly Brush StationTextBrush = Frozen(new SolidColorBrush(Color.FromRgb(0x2E, 0x7D, 0x32)));
    private static readonly Brush HintBrush = Frozen(new SolidColorBrush(Color.FromRgb(0x60, 0x60, 0x60)));
    private static readonly Pen GridLinePen = Frozen(new Pen(new SolidColorBrush(Color.FromRgb(0xE0, 0xE0, 0xE0)), 1));
    private static readonly Pen RobotOutlinePen = Frozen(new Pen(new SolidColorBrush(Color.FromRgb(0x20, 0x20, 0x20)), 1));

    // One colour per robot, repeating after ten robots.
    private static readonly Color[] RobotColors =
    [
        Color.FromRgb(0xD6, 0x27, 0x28), Color.FromRgb(0x1F, 0x77, 0xB4), Color.FromRgb(0x2C, 0xA0, 0x2C),
        Color.FromRgb(0xFF, 0x7F, 0x0E), Color.FromRgb(0x94, 0x67, 0xBD), Color.FromRgb(0x39, 0x3B, 0x79),
        Color.FromRgb(0xE3, 0x77, 0xC2), Color.FromRgb(0x17, 0xA2, 0xB8), Color.FromRgb(0x7F, 0x7F, 0x7F),
        Color.FromRgb(0x8C, 0x8D, 0x12),
    ];

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }

    // ---- Drawing ----

    protected override void OnRender(DrawingContext dc)
    {
        dc.DrawRectangle(BackgroundBrush, null, new Rect(RenderSize));

        if (Scenario is not { } scenario)
        {
            DrawCenteredText(dc, "Press Generate (or Load) to create a warehouse.", 16, HintBrush,
                new Point(ActualWidth / 2, ActualHeight / 2));
            return;
        }

        double cell = Math.Floor(Math.Min(ActualWidth / scenario.Width, ActualHeight / scenario.Height));
        if (cell < 3)
        {
            return; // window too small to draw anything useful
        }
        // Centre the grid in the available space.
        var origin = new Point(Math.Floor((ActualWidth - cell * scenario.Width) / 2),
            Math.Floor((ActualHeight - cell * scenario.Height) / 2));
        Rect CellRect(GridPosition p) => new(origin.X + p.X * cell, origin.Y + p.Y * cell, cell, cell);
        Point Center(GridPosition p) => new(origin.X + (p.X + 0.5) * cell, origin.Y + (p.Y + 0.5) * cell);

        DrawCells(dc, scenario, cell, CellRect, Center);

        IReadOnlyList<RobotState> robots = Robots ?? [];
        if (Plan is { Success: true } plan)
        {
            DrawPaths(dc, plan, cell, Center);
        }
        DrawPickupMarkers(dc, scenario, robots, cell, CellRect);
        DrawRobots(dc, robots, cell, Center);
    }

    private void DrawCells(DrawingContext dc, WarehouseScenario scenario, double cell, Func<GridPosition, Rect> cellRect,
        Func<GridPosition, Point> center)
    {
        for (int y = 0; y < scenario.Height; y++)
        {
            for (int x = 0; x < scenario.Width; x++)
            {
                var position = new GridPosition(x, y);
                CellKind kind = scenario[position];
                Brush brush = kind switch
                {
                    CellKind.Wall => WallBrush,
                    CellKind.Shelf => ShelfBrush,
                    CellKind.Station => StationBrush,
                    _ => FreeBrush,
                };
                dc.DrawRectangle(brush, cell >= 6 ? GridLinePen : null, cellRect(position));
                if (kind == CellKind.Station && cell >= 14)
                {
                    DrawCenteredText(dc, "P", cell * 0.45, StationTextBrush, center(position));
                }
            }
        }
    }

    private static void DrawPaths(DrawingContext dc, PlanOutcome plan, double cell, Func<GridPosition, Point> center)
    {
        for (int i = 0; i < plan.Routes.Count; i++)
        {
            IReadOnlyList<GridPosition> path = plan.Routes[i].Path;
            if (path.Count < 2)
            {
                continue;
            }
            var geometry = new StreamGeometry();
            using (StreamGeometryContext context = geometry.Open())
            {
                context.BeginFigure(center(path[0]), isFilled: false, isClosed: false);
                context.PolyLineTo(path.Skip(1).Select(center).ToList(), isStroked: true, isSmoothJoin: true);
            }
            geometry.Freeze();

            var pen = new Pen(new SolidColorBrush(WithAlpha(RobotColor(i), 90)), Math.Max(2, cell * 0.12))
            {
                LineJoin = PenLineJoin.Round,
                StartLineCap = PenLineCap.Round,
                EndLineCap = PenLineCap.Round,
            };
            pen.Freeze();
            dc.DrawGeometry(null, pen, geometry);
        }
    }

    // A hollow square in the robot's colour marks where it will pick up.
    // It disappears once the robot has been there.
    private static void DrawPickupMarkers(DrawingContext dc, WarehouseScenario scenario,
        IReadOnlyList<RobotState> robots, double cell, Func<GridPosition, Rect> cellRect)
    {
        for (int i = 0; i < scenario.Robots.Count; i++)
        {
            bool pickedUp = i < robots.Count && robots[i].HasPickedUp;
            if (pickedUp)
            {
                continue;
            }
            Rect rect = cellRect(scenario.Robots[i].Pickup);
            rect.Inflate(-cell * 0.18, -cell * 0.18);
            var pen = new Pen(new SolidColorBrush(RobotColor(i)), Math.Max(1.5, cell * 0.08));
            pen.Freeze();
            dc.DrawRectangle(null, pen, rect);
        }
    }

    private void DrawRobots(DrawingContext dc, IReadOnlyList<RobotState> robots, double cell,
        Func<GridPosition, Point> center)
    {
        double radius = cell * 0.38;
        foreach (RobotState robot in robots)
        {
            if (robot.Delivered)
            {
                continue; // delivered robots leave the grid
            }
            var fill = new SolidColorBrush(RobotColor(robot.Index));
            fill.Freeze();
            Point c = center(robot.Position);
            dc.DrawEllipse(fill, RobotOutlinePen, c, radius, radius);
            if (robot.HasPickedUp)
            {
                // A white ring shows the robot is carrying its item.
                var ring = new Pen(Brushes.White, Math.Max(1, cell * 0.06));
                ring.Freeze();
                dc.DrawEllipse(null, ring, c, radius * 0.78, radius * 0.78);
            }
            if (cell >= 12)
            {
                DrawCenteredText(dc, robot.Index.ToString(CultureInfo.InvariantCulture), cell * 0.36, Brushes.White, c,
                    bold: true);
            }
        }
    }

    private void DrawCenteredText(DrawingContext dc, string text, double size, Brush brush, Point center,
        bool bold = false)
    {
        var typeface = new Typeface(new FontFamily("Segoe UI"), FontStyles.Normal,
            bold ? FontWeights.Bold : FontWeights.Normal, FontStretches.Normal);
        var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, typeface,
            Math.Max(1, size), brush, VisualTreeHelper.GetDpi(this).PixelsPerDip);
        dc.DrawText(formatted, new Point(center.X - formatted.Width / 2, center.Y - formatted.Height / 2));
    }

    private static Color RobotColor(int index) => RobotColors[index % RobotColors.Length];

    private static Color WithAlpha(Color color, byte alpha) => Color.FromArgb(alpha, color.R, color.G, color.B);
}
