# Setting up OpenMPI and MPI4PY on a SteamLink

The process for enabling a Steam Link to act as part of a cluster is long. Every single dependancy for openMPI that is not already accessible on the Steam LInk will have to be manually added and compiled on the machine in order to "properly" run. 

These dependancies will have to be downloaded, transfered to the Steam Link via scp to a desired folder from another connected system, (I currently do not know where USB drives are located within the file system nor how to access or move them), and then from there they must be untared. From there installation will be similiar to the MPI guide below or instructions would be provided on the dependancies official website or github page.

#### An example scp command:

`scp /home/downloads/DEPENDENCY.tar.gz root@EXA.MPL.E.IP/usr/local/DESIRED FOLDER`
- Please refer to StaticIpSetup.md for how to set up a Static IP
- Please refer to ReadMe.md for password / how to change the password

#### Here are a list of required dependancies that will need to be build on the Steam Link:
GCC: https://github.com/gcc-mirror/gcc/archive/gcc-9_2_0-release.tar.gz <br/>
Perl: https://www.cpan.org/src/5.0/perl-5.30.0.tar.gz <br/>


## Installation of OpenMPI

Once you have installed all dependancies needed for openMPI the process is relatively simple as it is the same as a manual install for any other Single Board Computer.

#### Initial Setup

Before you begin you will want to create the following directories: <br />
`mkdir /usr/local/openMPI` <br />
`mkdir /usr/local/openMPI-dl` <br />
`mkdir /usr/local/openMPI-build` <br />

#### Local OpenMPI Installation

Begin on a terminal from the system that will SSH into the Steam Link by transfering the source code via SCP: <br />
`scp /home/downloads/openmpi-4.0.1.tar.gz root@EXA.MPL.E.IP/usr/local/openMPI-dl` <br/>

Proceed to SSH into your Steam Link and then move to your openMPI download folder: <br />
`cd /usr/local/openMPI-dl` <br />

Extract the file from the tar.gz: <br />
`tar xf openmpi-4.0.1.tar.gz` <br />

Move to your build folder: <br />
`cd /usr/local/openMPI-build` <br />

Run the Configuration Process: (This will take some time.) <br />
`/usr/local/openMPI-dl/openmpi-4.0/configure –prefix=/usr/local/openMPI` <br />

Make the build: (This will take a very long time.) <br />
`make` <br />

Install the build: <br />
`make all install`

#### Testing

Now you will want to OpenMPI to ensure all installed correctly. 
Run the following command: <br />
`mpiexec hostname` <br />
So long as this returns the hostname once then OpenMPI is installed.
