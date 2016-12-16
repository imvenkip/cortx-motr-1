Build and test environment for Mero
===================================

Overview
--------

This directory contains scripts for quick deployment of _Centos7_ virtual
machine, prepared for Mero development and testing. Support for real hardware
nodes provisioning may be added in future, as well as for _Amazon EC2_ nodes.

The virtual machine is automatically created from the official _Centos7_ base
image, which is downloaded from a public repository and is about 350MB in size.
After provisioning and installation of the required rpm packages, including
build tools and latest _Lustre_ from Intel's repository, it takes about 2GB of
disk space.

Requirements
------------

In order to run the scripts, additional tools have to be installed first. It's
assumed that either _Mac OS X_ or _Linux_ (_Ubuntu_) is used as the host operating
system. It should work on a _Windows_ host as well, though some additional
configuration steps may be required.

* Minimum Host OS
    - 8GB of RAM
    - 10GB of free disk space
    - 2 CPU cores

* Additional Software/Tools:
    - [VirtualBox](https://www.virtualbox.org/wiki/Downloads)
    - [Vagrant](https://www.vagrantup.com/downloads.html)
    - [Ansible](https://github.com/ansible/ansible)

On _Ubuntu_ host all of the above prerequisites can be installed with the single
command:

    sudo apt install virtualbox vagrant ansible

On _Mac OS X_ host the easiest way to install them is to download _VirtualBox_
and _Vagrant_ packages from their official web-sites (refer to the links above).

And install _Ansible_ using _Python's_ package manager (_PIP_), which is
available in _Mac OS X_ "out of the box":

    # install for current user only
    pip install --user ansible

    # install system-wide
    sudo pip install ansible

It's highly recommend to install the `vagrant-scp` plugin after completing
_Vagrant_ installation:

    vagrant plugin install vagrant-scp

It will make easier copying files between host and VM.

DevVM provisioning
------------------

After installing required tools from the above section, all that's left to do
is to run a _Vagrant_ command which will do remaining the work:

    cd $MERO_SRC/scripts/centos7/
    vagrant up

It will download latest _Centos7_ VM image from the official
[repository](https://atlas.hashicorp.com/centos/boxes/7) and configure it using
_Ansible_ "playbook" `scripts/centos7/devvm.yml`, which specifies all _Mero_
dependencies that should be installed in order to build and run _Mero_. It will
install _Lustre_ 2.9 from the official Intel's
[repository](https://downloads.hpdd.intel.com/public/lustre/lustre-2.9.0/el7/client/).

By default, _Vagrant_ will create `vagrant` user with password-less `sudo`
privileges. The user's password is `vagrant`.

Building and running Mero
-------------------------

After _devvm_ deployment, it can be accessed with the following command:

    vagrant ssh

_NOTE_: all _Vagrant_ commands need to be executed from the directory where
`Vagrantfile` file, which describes particular virtual machine, is located. In
our case it is the same directory where current `README.md` file is stored:
`scripts/centos7/`.

The simplest way to get _Mero_ sources on _devvm_ is to create an archive and
copy it via `scp`:

    # on the host
    tar -czf ~/mero.tar.gz $MERO_SRC
    cd $MERO_SRC/scripts/centos7/
    vagrant scp ~/mero.tar.gz :~

    # on devvm
    cd ~
    tar -xf mero.tar.gz
    cd mero
    ./autogen.sh && ./configure && make rpms-notests

Resulting _rpm_ files will be available in `~/rpmbuild/RPMS/x86_64/` directory.
To verify them they can be installed with:

    sudo yum install rpmbuild/RPMS/x86_64/*

Optional steps
--------------

For more convenient interaction with _devvm_ a shared network can be configured
in `Vagrantfile`. There are several options available. They are commented out by
default. Look for `config.vm.network`.
