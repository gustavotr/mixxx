// cachingreader.h
// Created 7/9/2009 by RJ Ryan (rryan@mit.edu)

#ifndef CACHINGREADER_H
#define CACHINGREADER_H

#include <QDebug>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QList>
#include <QVector>
#include <QLinkedList>
#include <QHash>
#include <QThread>

#include "defs.h"
#include "configobject.h"

class SoundSource;
class TrackInfoObject;
class ControlObjectThread;

// A Hint is an indication to the CachingReader that a certain section of a
// SoundSource will be used 'soon' and so it should be brought into memory by
// the reader work thread.
typedef struct Hint {
    // The sample to ensure is present in memory.
    int sample;
    // If a range of samples should be present, use length to indicate that the
    // range (sample, sample+length) should be present in memory.
    int length;
    // Currently unused -- but in the future could be used to prioritize certain
    // hints over others. A priority of 1 is the highest priority and should be
    // used for samples that will be read imminently. Hints for samples that
    // have the potential to be read (i.e. a cue point) should be issued with
    // priority >10.
    int priority;
} Hint;

// A Chunk is a section of audio that is being cached. The chunk_number can be
// used to figure out the sample number of the first sample in data by using
// sampleForChunk()
typedef struct Chunk {
    int chunk_number;
    int length;
    CSAMPLE* data;
    Chunk* prev_lru;
    Chunk* next_lru;
} Chunk;

// CachingReader provides a layer on top of a SoundSource for reading samples
// from a file. A cache is provided so that repeated reads to a certain section
// of a song do not cause disk seeks or unnecessary SoundSource
// calls. CachingReader provides a worker thread that can be used to prepare the
// cache so that areas of a file that will soon be read are present in memory
// once they are needed. This can be accomplished by issueing 'hints' to the
// reader of areas of a SoundSource that will be read soon.
class CachingReader : public QThread {
    Q_OBJECT

  public:

    // Construct a CachingReader with the given group. The group is only
    // necessary for legacy reasons. The CachingReader will clear the
    // _group,'TrackEnd' control object when it is done loading a new track.
    CachingReader(const char* _group,
                  ConfigObject<ConfigValue>* _config);
    virtual ~CachingReader();

    // You really shouldn't use these unless there /really/ isn't any other way
    // of getting at this data. Calling it involves a lock/unlock. It's better
    // to receive trackLoaded signals instead.
    int getTrackSampleRate();
    int getTrackNumSamples();

    // Read num_samples from the SoundSource starting with sample into
    // buffer. Returns the total number of samples actually written to buffer.
    int read(int sample, int num_samples, CSAMPLE* buffer);


    // Issue a hint or list of hints to the CachingReader. Next time the reader
    // is woken, it will make sure as many hints are in memory as possible. The
    // hint's sample and length are taken into account, but priority is
    // currently unused.
    void hint(Hint& hint);
    void hint(QList<Hint>& hintList);

    // Request that the CachingReader load a new track. These requests are
    // processed in the work thread, so the reader must be woken up via wake()
    // for this to take effect.
    void newTrack(TrackInfoObject* pTrack);

    // Wake the reader up so that it will process newTrack requests and hints.
    void wake();

  signals:
    // Emitted once a new track is loaded and ready to be read from.
    void trackLoaded(TrackInfoObject *pTrack, int iSampleRate, int iNumSamples);

  protected:
    void run();

  private:

    // A Chunk is a memory-resident section of audio that has been cached. Each
    // chunk holds a fixed number of samples given by kSamplesPerChunk.
    const static int kChunkLength, kSamplesPerChunk;

    // Removes a chunk from the LRU list
    static Chunk* removeFromLRUList(Chunk* chunk, Chunk* head);
    // Inserts a chunk into the LRU list
    static Chunk* insertIntoLRUList(Chunk* chunk, Chunk* head);

    // Initialize the reader by creating all the chunks from the RAM provided to
    // the CachingReader.
    void initialize();

    // Stop the reader thread. This will block until the thread is stopped.
    void stop();

    // Internal method to load a track. Emits trackLoaded when finished.
    void loadTrack(TrackInfoObject *pTrack);

    // Queue of recent hints, and the corresponding lock.
    QMutex m_hintQueueMutex;
    QQueue<Hint> m_hintQueue;

    // Queue of Tracks to load, and the corresponding lock.
    QMutex m_trackQueueMutex;
    QQueue<TrackInfoObject*> m_trackQueue;

    // Held when the Reader is working: read() vs. run()
    QMutex m_readerMutex;
    QWaitCondition m_readerWait;

    //
    // Everything below this line is guarded by m_readerMutex, make sure you
    // hold it if you touch them.
    //

    // Look up chunk_number at any cost. Reads from SoundSource if
    // necessary. Returns NULL if chunk_number is not valid.
    Chunk* getChunk(int chunk_number);

    // Read the given chunk_number from the file into pChunk's data
    // buffer. Fills length/sample information about Chunk* as well.
    bool readChunkFromFile(Chunk* pChunk, int chunk_number);

    // Looks for the provided chunk number in the index of in-memory chunks and
    // returns it if it is present. If not, returns NULL.
    Chunk* lookupChunk(int chunk_number);

    // Returns a Chunk to the free list
    void freeChunk(Chunk* pChunk);

    // Returns all allocated chunks to the free list
    void freeAllChunks();

    // Gets a chunk from the free list. Returns NULL if none available.
    Chunk* allocateChunk();

    // Gets a chunk from the free list, frees the LRU Chunk if none available.
    Chunk* allocateChunkExpireLRU();

    // Given a sample number, return the chunk number corresponding to it.
    int chunkForSample(int sample_number) {
        return int(floor(double(sample_number) / double(kSamplesPerChunk)));
    }

    // Given a chunk number, return the start sample number for the chunk.
    int sampleForChunk(int chunk_number) {
        return chunk_number * kSamplesPerChunk;
    }

    const char* m_pGroup;
    const ConfigObject<ConfigValue>* m_pConfig;

    // The current track loaded
    TrackInfoObject* m_pCurrentTrack;
    ControlObjectThread* m_pTrackEnd;
    // The current sound source of the track loaded
    SoundSource* m_pCurrentSoundSource;
    int m_iTrackSampleRate;
    int m_iTrackNumSamples;

    // Keeps track of free Chunks we've allocated
    QVector<Chunk*> m_chunks;
    // List of free chunks available for use.
    QList<Chunk*> m_freeChunks;
    // Keeps track of what Chunks we've allocated and indexes them based on what
    // chunk number they are allocated to.
    QHash<int, Chunk*> m_allocatedChunks;

    // The linked list of recently-used chunks.
    Chunk* m_mruChunk;
    Chunk* m_lruChunk;

    // The raw memory buffer which is divided up into chunks.
    CSAMPLE* m_pRawMemoryBuffer;
    int m_iRawMemoryBufferLength;
    // Temporary buffer for reading from SoundSources
    SAMPLE* m_pSample;
    bool m_bQuit;
};


#endif /* CACHINGREADER_H */
