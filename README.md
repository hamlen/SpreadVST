# SpreadVST

*Spread* is a VST3 that distributes incoming MIDI notes across a spectrum of output channels ranging from 1 to the value of the plug-in's **OutChannels** parameter (up to 16).  This can be useful for improving performance and reducing audio glitches on multicore systems, because it can distribute the incoming load of notes across multiple instances of a virtual instrument, each of which can be run on a separate core by your VST host.

For example, if you have a virtual instrument that is single-core or that doesn't make maximal use of all your computer's cores, sometimes performance will improve by creating *n* copies of the instrument in your host, sending the incoming notes to *Spread*, and sending each of the *n* output channels from *Spread* to each respective instrument copy. Most hosts try to run separate instrument instances on separate cores, so this arrangement can sometimes make more productive use of all your cores, avoiding CPU overloads that produce audio glitches and drop-outs.  For best results, *n* should usually be some number less than the number of available cores.

*Spread*'s note distribution strategy is dictated by its **Strategy** parameter, which can be set to any one of the following:
- The **Round-robin** strategy allocates each successive input note to each successive output channel in a continuous rotation.
- The **Random** strategy allocates each input note to a pseudo-randomly chosen output channel.  The randomness is uniform but deterministic, so that a given sequence of input notes received during the lifetime of the plug-in should yield the same sequence of pseudo-random output channels every time.
- The **Min-Load** strategy tries to track a running tally of the notes currently sounding on each output channel, and allocates each input note to a minimally loaded output channel.

Sustain pedal events sent to *Spread* are rebroadcast on all output channels, and sustained notes count towards each output channel's load until the pedal is released when using the **Min-Load** strategy. (To disregard sustain pedal events, just filter them out of the MIDI input stream to *Spread*.)

Setting the **OutChannels** parameter to zero puts the plug-in in a bypass mode that simply preserves the channel of each input note. Sending an All Sounds Off (MIDI 120) or Reset All (MIDI 121) message to *Spread* causes it to send note-off events for all currently held notes and re-initialize any internal state associated with its channel distribution strategy (e.g., restart the random channel selection sequence for the **Random** strategy).
