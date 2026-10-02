using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Mesen.Config;

namespace Mesen.Windows
{
	public class McpConfigWindow : MesenWindow
	{
		public McpConfigWindow()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		private void Ok_OnClick(object sender, RoutedEventArgs e)
		{
			McpConfig cfg = (McpConfig)DataContext!;
			ConfigManager.Config.Mcp.Port = cfg.Port;
			Close(true);
		}

		private void Cancel_OnClick(object sender, RoutedEventArgs e)
		{
			Close(false);
		}
	}
}
