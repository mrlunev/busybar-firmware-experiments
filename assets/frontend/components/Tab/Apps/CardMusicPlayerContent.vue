<template>
  <div class="flex flex-col gap-3">
    <div v-if="isLoading" class="flex items-center gap-3 py-2">
      <UIcon name="i-bi-loader" class="size-5 animate-spin" />
      <span class="text-muted">Loading tracks...</span>
    </div>

    <div v-else-if="tracks.length === 0" class="text-muted text-sm py-2">
      No tracks uploaded yet. Upload an MP3 file to get started.
    </div>

    <div v-else class="flex flex-col gap-1 rounded-group">
      <div
        v-for="track in tracks"
        :key="track.name"
        class="flex items-center justify-between gap-3 bg-accented/25 dark:bg-elevated/75 p-3 rounded-xl"
      >
        <div class="flex items-center gap-3 min-w-0">
          <UIcon name="i-bi-music-note" class="size-5 shrink-0 text-primary-500" />
          <div class="min-w-0">
            <div class="truncate">{{ track.name }}</div>
            <div class="text-sm text-muted">{{ formatSize(track.size) }}</div>
          </div>
        </div>
        <UButton
          icon="i-bi-trash3"
          variant="ghost"
          color="error"
          size="sm"
          square
          :loading="track.deleting"
          @click="deleteTrack(track)"
        />
      </div>
    </div>

    <div class="flex items-center gap-2">
      <UButton
        label="Upload Track"
        icon="i-bi-upload"
        :loading="isUploading"
        @click="triggerUpload"
      />
      <span v-if="uploadError" class="text-error text-sm">{{ uploadError }}</span>
    </div>

    <input
      ref="fileInput"
      type="file"
      accept=".mp3"
      class="hidden"
      @change="handleFileSelected"
    >
  </div>
</template>

<script setup lang="ts">
const props = defineProps<{
  appId: string;
}>();

const apiStore = useApiStore();

interface TrackEntry {
  name: string;
  size: number;
  deleting: boolean;
}

const APP_PATH = `/ext/apps/${props.appId}`;

const tracks = ref<TrackEntry[]>([]);
const isLoading = ref(false);
const isUploading = ref(false);
const uploadError = ref<string | null>(null);
const fileInput = ref<HTMLInputElement | null>(null);

function formatSize (bytes: number): string {
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

async function loadTracks () {
  isLoading.value = true;
  try {
    const listing = await apiStore.apiRequest<{ list: Array<{ name: string; type: string; size?: number }> }>(`/api/storage/list?path=${encodeURIComponent(APP_PATH)}`);
    tracks.value = listing.list
      .filter(item => item.type === 'file' && item.name.toLowerCase().endsWith('.mp3'))
      .map(item => ({ name: item.name, size: item.size ?? 0, deleting: false }))
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch {
    tracks.value = [];
  } finally {
    isLoading.value = false;
  }
}

function triggerUpload () {
  uploadError.value = null;
  fileInput.value?.click();
}

async function handleFileSelected (event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }

  if (!file.name.toLowerCase().endsWith('.mp3')) {
    uploadError.value = 'Only MP3 files are supported';
    input.value = '';
    return;
  }

  isUploading.value = true;
  uploadError.value = null;

  try {
    const maxPath = 63;
    let fileName = file.name.replace(/[^a-zA-Z0-9._-]/g, '_');
    const maxNameLen = maxPath - APP_PATH.length - 1;
    if (fileName.length > maxNameLen) {
      fileName = fileName.substring(0, maxNameLen - 4) + '.mp3';
    }
    await apiStore.apiRequest(
      `/api/storage/write?path=${encodeURIComponent(`${APP_PATH}/${fileName}`)}`,
      { method: 'POST', body: file }
    );
    await loadTracks();
  } catch (e) {
    uploadError.value = e instanceof Error ? e.message : 'Upload failed';
  } finally {
    isUploading.value = false;
    input.value = '';
  }
}

async function deleteTrack (track: TrackEntry) {
  track.deleting = true;
  uploadError.value = null;
  try {
    await apiStore.apiRequest(
      `/api/storage/remove?path=${encodeURIComponent(`${APP_PATH}/${track.name}`)}`,
      { method: 'DELETE' }
    );
    tracks.value = tracks.value.filter(t => t.name !== track.name);
  } catch (e) {
    track.deleting = false;
    uploadError.value = e instanceof Error ? e.message : 'Delete failed';
  }
}

onMounted(() => {
  loadTracks();
});
</script>
