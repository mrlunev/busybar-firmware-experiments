<template>
  <SectionCard
    :data-id="`apps-section-${app.id}`"
    :title="app.name"
    :subtitle="app.schema.description"
    icon="i-bi-gear"
  >
    <template v-if="showStandardActions" #actions>
      <UButton
        label="Save"
        icon="i-bi-check-circle"
        :loading="app.saving"
        :disabled="!isDirty"
        @click="handleSave"
      />
      <UButton
        v-if="isDirty"
        label="Reset"
        variant="outline"
        color="neutral"
        icon="i-bi-arrow-counterclockwise"
        @click="handleReset"
      />
    </template>

    <template v-if="useRawBody" #raw-body>
      <template v-if="app.schema.component === 'music_player'">
        <TabAppsCardMusicPlayerContent :app-id="app.id" />
      </template>
      <template v-else>
        <div class="grid sm:grid-cols-2 gap-6">
          <TabAppsDynamicField
            v-for="field in app.schema.fields"
            :key="field.key"
            :field="field"
            :model-value="app.config[field.key]"
            @update:model-value="handleFieldUpdate(field.key, $event)"
          />
        </div>
      </template>
    </template>

    <template v-if="useDefaultSlot">
      <div
        v-for="field in app.schema.fields"
        :key="field.key"
        class="flex items-center justify-between gap-4"
      >
        <TabAppsDynamicField
          :field="field"
          :model-value="app.config[field.key]"
          @update:model-value="handleFieldUpdate(field.key, $event)"
        />
      </div>
    </template>

    <p v-if="app.saveError" class="text-error text-sm">
      {{ app.saveError }}
    </p>
  </SectionCard>
</template>

<script setup lang="ts">
import type { AppConfigValue, DiscoveredApp } from '~/stores/appConfigStore';

const props = defineProps<{
  app: DiscoveredApp;
}>();

const appConfigStore = useAppConfigStore();

const hasFields = computed(() => props.app.schema.fields.length > 0);
const useRawBody = computed(() => !!props.app.schema.component || (props.app.schema.layout === 'columns' && hasFields.value));
const showStandardActions = computed(() => hasFields.value && !props.app.schema.component);
const useDefaultSlot = computed(() => !props.app.schema.component && props.app.schema.layout !== 'columns' && hasFields.value);

const savedSnapshot = ref<string>('');

const isDirty = computed(() => {
  return JSON.stringify(props.app.config) !== savedSnapshot.value;
});

function takeSnapshot () {
  savedSnapshot.value = JSON.stringify(props.app.config);
}

function handleFieldUpdate (key: string, value: AppConfigValue) {
  appConfigStore.updateField(props.app.id, key, value);
}

async function handleSave () {
  const saved = await appConfigStore.saveAppConfig(props.app.id);
  if (saved) {
    takeSnapshot();
  }
}

function handleReset () {
  try {
    const original = JSON.parse(savedSnapshot.value);
    for (const key of Object.keys(original)) {
      appConfigStore.updateField(props.app.id, key, original[key]);
    }
  } catch {
    // Ignore an invalid snapshot.
  }
}

onMounted(() => {
  takeSnapshot();
});

watch(() => props.app.id, () => {
  takeSnapshot();
});
</script>
