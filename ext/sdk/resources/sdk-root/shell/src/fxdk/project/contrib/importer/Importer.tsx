import * as React from 'react';
import { Modal } from 'fxdk/ui/Modal/Modal';
import { AssetImporterType, assetImporterTypes } from 'shared/asset.types';
import { GitImporter } from './GitImporter';
import { ImporterProps } from './Importer.types';
import { FsImporter } from './FsImporter';
import { ExampleImporter } from './ExampleImporter';
import { TabItem, TabSelector } from 'fxdk/ui/controls/TabSelector/TabSelector';
import { observer } from 'mobx-react-lite';
import { ImporterState } from './ImporterState';
import s from './Importer.module.scss';

const importerTypeOptions: TabItem[] = [
  {
    label: 'Import example',
    value: assetImporterTypes.example,
  },
  {
    label: 'Import from Git repository',
    value: assetImporterTypes.git,
  },
  {
    label: 'Import from file system on this PC',
    value: assetImporterTypes.fs,
  },
];

const importerRenderers: Record<AssetImporterType, React.ComponentType<ImporterProps>> = {
  [assetImporterTypes.git]: GitImporter,
  [assetImporterTypes.fs]: FsImporter,
  [assetImporterTypes.example]: ExampleImporter,
};

export const Importer = observer(function Importer() {
  const [importerType, setImporterType] = React.useState<AssetImporterType>(assetImporterTypes.example);

  const ImporterRenderer = importerRenderers[importerType];

  return (
    <Modal fullWidth onClose={ImporterState.close}>
      <div className={s.root}>
        <div className="modal-header">
          Import asset
        </div>

        <div className="modal-block">
          <TabSelector
            value={importerType}
            items={importerTypeOptions}
            onChange={setImporterType}
          />
        </div>

        <ImporterRenderer onClose={ImporterState.close} />
      </div>
    </Modal>
  );
});
