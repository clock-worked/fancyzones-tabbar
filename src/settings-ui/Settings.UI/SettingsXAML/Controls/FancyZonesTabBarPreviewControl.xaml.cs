// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace Microsoft.PowerToys.Settings.UI.Controls
{
    public sealed partial class FancyZonesTabBarPreviewControl : UserControl
    {
        public FancyZonesTabBarPreviewControl()
        {
            InitializeComponent();
            Update();
        }

        public bool TabBarFillZoneWidth
        {
            get => (bool)GetValue(TabBarFillZoneWidthProperty);
            set => SetValue(TabBarFillZoneWidthProperty, value);
        }

        public static readonly DependencyProperty TabBarFillZoneWidthProperty =
            DependencyProperty.Register(nameof(TabBarFillZoneWidth), typeof(bool), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(true, OnPropertyChanged));

        public double TabBarTabWidth
        {
            get => (double)GetValue(TabBarTabWidthProperty);
            set => SetValue(TabBarTabWidthProperty, value);
        }

        public static readonly DependencyProperty TabBarTabWidthProperty =
            DependencyProperty.Register(nameof(TabBarTabWidth), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(200d, OnPropertyChanged));

        public double TabBarTextSize
        {
            get => (double)GetValue(TabBarTextSizeProperty);
            set => SetValue(TabBarTextSizeProperty, value);
        }

        public static readonly DependencyProperty TabBarTextSizeProperty =
            DependencyProperty.Register(nameof(TabBarTextSize), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(14d, OnPropertyChanged));

        public double TabBarIconSize
        {
            get => (double)GetValue(TabBarIconSizeProperty);
            set => SetValue(TabBarIconSizeProperty, value);
        }

        public static readonly DependencyProperty TabBarIconSizeProperty =
            DependencyProperty.Register(nameof(TabBarIconSize), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(16d, OnPropertyChanged));

        public double TabBarIconHorizontalSpacing
        {
            get => (double)GetValue(TabBarIconHorizontalSpacingProperty);
            set => SetValue(TabBarIconHorizontalSpacingProperty, value);
        }

        public static readonly DependencyProperty TabBarIconHorizontalSpacingProperty =
            DependencyProperty.Register(nameof(TabBarIconHorizontalSpacing), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(8d, OnPropertyChanged));

        public double TabBarIconVerticalSpacing
        {
            get => (double)GetValue(TabBarIconVerticalSpacingProperty);
            set => SetValue(TabBarIconVerticalSpacingProperty, value);
        }

        public static readonly DependencyProperty TabBarIconVerticalSpacingProperty =
            DependencyProperty.Register(nameof(TabBarIconVerticalSpacing), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(4d, OnPropertyChanged));

        public string TabBarFocusColor
        {
            get => (string)GetValue(TabBarFocusColorProperty);
            set => SetValue(TabBarFocusColorProperty, value);
        }

        public static readonly DependencyProperty TabBarFocusColorProperty =
            DependencyProperty.Register(nameof(TabBarFocusColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#0078D7", OnPropertyChanged));

        public double TabBarHeight
        {
            get => (double)GetValue(TabBarHeightProperty);
            set => SetValue(TabBarHeightProperty, value);
        }

        public static readonly DependencyProperty TabBarHeightProperty =
            DependencyProperty.Register(nameof(TabBarHeight), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(0d, OnPropertyChanged));

        public double TabBarCornerRadius
        {
            get => (double)GetValue(TabBarCornerRadiusProperty);
            set => SetValue(TabBarCornerRadiusProperty, value);
        }

        public static readonly DependencyProperty TabBarCornerRadiusProperty =
            DependencyProperty.Register(nameof(TabBarCornerRadius), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(8d, OnPropertyChanged));

        public string TabBarUnfocusedColor
        {
            get => (string)GetValue(TabBarUnfocusedColorProperty);
            set => SetValue(TabBarUnfocusedColorProperty, value);
        }

        public static readonly DependencyProperty TabBarUnfocusedColorProperty =
            DependencyProperty.Register(nameof(TabBarUnfocusedColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#E6E6E6", OnPropertyChanged));

        public string TabBarFocusedTextColor
        {
            get => (string)GetValue(TabBarFocusedTextColorProperty);
            set => SetValue(TabBarFocusedTextColorProperty, value);
        }

        public static readonly DependencyProperty TabBarFocusedTextColorProperty =
            DependencyProperty.Register(nameof(TabBarFocusedTextColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#FFFFFF", OnPropertyChanged));

        public string TabBarUnfocusedTextColor
        {
            get => (string)GetValue(TabBarUnfocusedTextColorProperty);
            set => SetValue(TabBarUnfocusedTextColorProperty, value);
        }

        public static readonly DependencyProperty TabBarUnfocusedTextColorProperty =
            DependencyProperty.Register(nameof(TabBarUnfocusedTextColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#000000", OnPropertyChanged));

        public double TabBarIconLeftSpacing
        {
            get => (double)GetValue(TabBarIconLeftSpacingProperty);
            set => SetValue(TabBarIconLeftSpacingProperty, value);
        }

        public static readonly DependencyProperty TabBarIconLeftSpacingProperty =
            DependencyProperty.Register(nameof(TabBarIconLeftSpacing), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(8d, OnPropertyChanged));

        public double TabBarCloseButtonSpacing
        {
            get => (double)GetValue(TabBarCloseButtonSpacingProperty);
            set => SetValue(TabBarCloseButtonSpacingProperty, value);
        }

        public static readonly DependencyProperty TabBarCloseButtonSpacingProperty =
            DependencyProperty.Register(nameof(TabBarCloseButtonSpacing), typeof(double), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(8d, OnPropertyChanged));

        public string TabBarCloseButtonColor
        {
            get => (string)GetValue(TabBarCloseButtonColorProperty);
            set => SetValue(TabBarCloseButtonColorProperty, value);
        }

        public static readonly DependencyProperty TabBarCloseButtonColorProperty =
            DependencyProperty.Register(nameof(TabBarCloseButtonColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#000000", OnPropertyChanged));

        public string TabBarCloseButtonBackgroundColor
        {
            get => (string)GetValue(TabBarCloseButtonBackgroundColorProperty);
            set => SetValue(TabBarCloseButtonBackgroundColorProperty, value);
        }

        public static readonly DependencyProperty TabBarCloseButtonBackgroundColorProperty =
            DependencyProperty.Register(nameof(TabBarCloseButtonBackgroundColor), typeof(string), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata("#000000", OnPropertyChanged));

        public int TabBarCloseButtonBackgroundShape
        {
            get => (int)GetValue(TabBarCloseButtonBackgroundShapeProperty);
            set => SetValue(TabBarCloseButtonBackgroundShapeProperty, value);
        }

        public static readonly DependencyProperty TabBarCloseButtonBackgroundShapeProperty =
            DependencyProperty.Register(nameof(TabBarCloseButtonBackgroundShape), typeof(int), typeof(FancyZonesTabBarPreviewControl), new PropertyMetadata(1, OnPropertyChanged));

        private static void OnPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is FancyZonesTabBarPreviewControl control)
            {
                control.Update();
            }
        }

        private void FancyZonesTabBarPreviewControl_Loaded(object sender, RoutedEventArgs e)
        {
            Update();
        }

        private void Update()
        {
            RootStackPanel.Children.Clear();

            // Account for the StackPanel's Spacing="8" (2 gaps between 3 tabs = 16px total)
            // plus the StackPanel's right Margin (4px) so the rightmost tab's close button
            // is not clipped at the container edge.
            var tabWidth = TabBarFillZoneWidth ? Math.Max(100d, (ActualWidth > 0 ? ActualWidth - 20 : 240d) / 3) : TabBarTabWidth;
            var tabHeight = TabBarHeight > 0 ? TabBarHeight : Math.Max(28d, TabBarTextSize + 18d);
            var unfocusedBrush = new SolidColorBrush(ParseColor(TabBarUnfocusedColor, Colors.LightGray));
            var focusedTextBrush = new SolidColorBrush(ParseColor(TabBarFocusedTextColor, Colors.White));
            var unfocusedTextBrush = new SolidColorBrush(ParseColor(TabBarUnfocusedTextColor, Colors.Black));
            var focusColor = ParseColor(TabBarFocusColor, Color.FromArgb(255, 0, 120, 215));
            var focusBrush = new SolidColorBrush(focusColor);
            var closeButtonBrush = new SolidColorBrush(ParseColor(TabBarCloseButtonColor, Colors.Black));
            var closeButtonBackgroundBrush = new SolidColorBrush(ParseColor(TabBarCloseButtonBackgroundColor, Colors.Black)) { Opacity = 0.15 };

            var tabs = new[] { "Home", "Files", "Settings" };
            for (var index = 0; index < tabs.Length; index++)
            {
                var isActive = index == 0;
                var tab = new Border
                {
                    Width = tabWidth,
                    Height = tabHeight,
                    CornerRadius = new CornerRadius(TabBarCornerRadius, TabBarCornerRadius, 0, 0),
                    Background = isActive ? focusBrush : unfocusedBrush,
                    Margin = new Thickness(0, TabBarIconVerticalSpacing / 2, 0, 0),
                };

                var grid = new Grid
                {
                    Padding = new Thickness(TabBarIconLeftSpacing, 0, 4, 0),
                };
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

                var content = new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    HorizontalAlignment = HorizontalAlignment.Left,
                    VerticalAlignment = VerticalAlignment.Center,
                    Spacing = TabBarIconHorizontalSpacing,
                };

                var icon = new TextBlock
                {
                    Text = index == 0 ? "H" : index == 1 ? "F" : "S",
                    FontSize = TabBarIconSize,
                    FontWeight = FontWeights.SemiBold,
                    Foreground = isActive ? focusedTextBrush : unfocusedTextBrush,
                    VerticalAlignment = VerticalAlignment.Center,
                };

                var label = new TextBlock
                {
                    Text = tabs[index],
                    FontSize = TabBarTextSize,
                    Foreground = isActive ? focusedTextBrush : unfocusedTextBrush,
                    VerticalAlignment = VerticalAlignment.Center,
                };

                content.Children.Add(icon);
                content.Children.Add(label);
                Grid.SetColumn(content, 0);
                grid.Children.Add(content);

                if (TabBarCloseButtonBackgroundShape != 0)
                {
                    var closeSize = Math.Max(12d, tabHeight * 0.4);
                    var closeBorder = new Border
                    {
                        Width = closeSize,
                        Height = closeSize,
                        Margin = new Thickness(0, 0, TabBarCloseButtonSpacing, 0),
                        HorizontalAlignment = HorizontalAlignment.Right,
                        VerticalAlignment = VerticalAlignment.Center,
                        Background = closeButtonBackgroundBrush,
                        CornerRadius = TabBarCloseButtonBackgroundShape == 1 ? new CornerRadius(closeSize / 2) : new CornerRadius(2),
                        Child = new TextBlock
                        {
                            Text = "X",
                            FontSize = Math.Max(8d, TabBarTextSize * 0.7),
                            FontWeight = FontWeights.Bold,
                            Foreground = closeButtonBrush,
                            HorizontalAlignment = HorizontalAlignment.Center,
                            VerticalAlignment = VerticalAlignment.Center,
                        },
                    };
                    Grid.SetColumn(closeBorder, 2);
                    grid.Children.Add(closeBorder);
                }

                tab.Child = grid;
                RootStackPanel.Children.Add(tab);
            }
        }

        private static Color ParseColor(string value, Color fallback)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return fallback;
            }

            var trimmed = value.Trim();
            if (trimmed.StartsWith('#'))
            {
                trimmed = trimmed.Substring(1);
            }

            if (trimmed.Length == 6 && int.TryParse(trimmed, System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out var packed))
            {
                return Color.FromArgb(
                    255,
                    (byte)((packed >> 16) & 0xFF),
                    (byte)((packed >> 8) & 0xFF),
                    (byte)(packed & 0xFF));
            }

            return fallback;
        }
    }
}
