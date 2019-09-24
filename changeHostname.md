# Changing the Steam Link Hostname

The process to change the steamlink hostname is relatively easy. 

The process is as simple as follows:

After SSH'ing into the Steam Link proceed to the following file path.

`cd /etc`

From here enter the command

`echo "DESIRED HOSTNAME HERE" > hostname`

Finally, reboot the system.

The same command may need to be done but replacing hostname with hosts.
