namespace Consumer
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.getPortsButton = new System.Windows.Forms.Button();
            this.SuspendLayout();
            // 
            // getPortsButton
            // 
            this.getPortsButton.Location = new System.Drawing.Point(104, 233);
            this.getPortsButton.Name = "getPortsButton";
            this.getPortsButton.Size = new System.Drawing.Size(228, 74);
            this.getPortsButton.TabIndex = 0;
            this.getPortsButton.Text = "GetPorts";
            this.getPortsButton.UseVisualStyleBackColor = true;
            this.getPortsButton.Click += new System.EventHandler(this.getPortsButton_Click);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(9F, 20F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(425, 319);
            this.Controls.Add(this.getPortsButton);
            this.Name = "Form1";
            this.Text = "Form1";
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.Button getPortsButton;
    }
}

