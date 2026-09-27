using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace Mesen.Debugger.StatusViews
{
	public class Pic18StatusView : UserControl
	{
		public Pic18StatusView()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}
	}
}