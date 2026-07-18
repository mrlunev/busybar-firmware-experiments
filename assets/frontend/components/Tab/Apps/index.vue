<template>
  <div v-if="appConfigStore.isLoading" class="flex items-center gap-3 py-4">
    <UIcon name="i-bi-loader" class="size-5 animate-spin" />
    <span class="text-muted">Discovering apps...</span>
  </div>

  <div v-else-if="appConfigStore.error" class="text-error py-4">
    {{ appConfigStore.error }}
  </div>

  <div v-else-if="appConfigStore.apps.length === 0" class="text-muted py-4">
    No configurable apps found.
  </div>

  <template v-else>
    <TabAppsCardDynamicApp
      v-for="app in appConfigStore.apps"
      :key="app.id"
      :app="app"
    />
  </template>
</template>

<script setup lang="ts">
const appConfigStore = useAppConfigStore();

onMounted(() => {
  appConfigStore.discoverApps();
});
</script>
