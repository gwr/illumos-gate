#!/usr/bin/perl -w
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2026 Gordon W. Ross
#

#
# intrd-test.pl - scenario adapter for intrd -S
#
# This file is do'd by intrd.pl via: intrd -D -S intrd-test.pl <input.kstats>
# When do'd, @ARGV = ("intrd-test.pl", "<input.kstats>").
#
# Provides stubs for myks_update(), syslog(), intrmove(), and is_apic().
# Reads test input in kstat -p format (see doc/intrd-test-plan.md).
#

use strict;

my $input_file = $ARGV[1];
die "Usage: intrd -D -S intrd-test.pl <input.kstats>\n"
    unless defined $input_file;

#
# Parse the input file into an array of snapshot hashes.
#
# Lines beginning with "# sample N" delimit snapshots.
# Lines of the form module:instance:name:stat\tvalue are kstat entries.
# intrd_test:0:config entries are consumed here as config, not kstats.
#

my @snapshots;
my $is_apic_val = 0;
my $fail_move_count = 0;

{
    open(my $fh, '<', $input_file) or die "Cannot open $input_file: $!\n";

    my $snap;
    while (<$fh>) {
        chomp;
        next if /^\s*$/;

        if (/^# sample \d+/) {
            push @snapshots, $snap if $snap;
            $snap = {};
            next;
        }

        next if /^#/;

        next unless /^(\S+):(\d+):(\S+):(\S+)\t(.*)$/;
        my ($module, $instance, $name, $stat, $value) = ($1, $2, $3, $4, $5);

        if ($module eq 'intrd_test') {
            $is_apic_val = int($value) if $stat eq 'is_apic';
            $fail_move_count = int($value) if $stat eq 'fail_move_count';
            next;
        }

        $snap = {} unless defined $snap;

        # intrd matches against null-terminated strings for these fields.
        if ($module eq 'cpu_info' && $stat eq 'state') {
            $value .= "\0";
        } elsif ($module eq 'pci_intrs' && ($stat eq 'type' || $stat eq 'name')) {
            $value .= "\0";
        }

        $snap->{$module}{$instance}{$name}{$stat} = $value;
    }
    push @snapshots, $snap if $snap;
    close $fh;
}

my $snap_idx = 0;

sub myks_update {
    exit(0) if $snap_idx >= scalar @snapshots;
    return $snapshots[$snap_idx++];
}

sub syslog {
    my ($level, $fmt, @args) = @_;
    if ($level eq 'debug') {
        # Only pass through debug lines that reflect intrd decisions.
        # Suppress dumpdelta noise, separators, and reconfig internals.
        return unless
            $fmt =~ /^GOODNESS:/ ||
            $fmt =~ /^goodness / ||
            $fmt =~ /^setting new baseline/ ||
            $fmt =~ /^evaluating interrupt/ ||
            $fmt =~ /^intrd is starting/ ||
            $fmt =~ /^sleeptime:/ ||
            $fmt =~ /^do_reconfig FAILED/;
    }
    printf("syslog $level $fmt\n", @args);
}

sub intrmove {
    my ($buspath, $oldcpu, $ino, $newcpu, $num_ino) = @_;
    printf("intrmove: buspath=%s oldcpu=%d ino=%d newcpu=%d num_ino=%d\n",
        $buspath, $oldcpu, $ino, $newcpu, $num_ino);
    if ($fail_move_count > 0) {
        $fail_move_count--;
        return 0;
    }
    return 1;
}

sub is_apic {
    return $is_apic_val;
}
